#include <game/aimbot.h>
#include <game/cfg.h>
#include <game/game.h>
#include <hooks/hooks.h>
#include <menu/menu.h>
#include <sdk/input.h>
#include <sdk/offsets.h>
#include <sdk/sdk.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

// [silent] 本 tick 目标角（定义见 aimbot.h 注释）
bool aimbot_t::silent_valid = false;
float aimbot_t::silent_pitch = 0.f;
float aimbot_t::silent_yaw = 0.f;
bool aimbot_t::wants_autofire = false;
bool aimbot_t::vis_valid = false;
bool aimbot_t::vis_safe = false;
sdk::vector aimbot_t::vis_pos{};
bool aimbot_t::wants_autostop = false;

namespace
{
	

	
	constexpr float k_rad2deg = 180.f / 3.14159265f;

	// 骨骼关节布局同 visuals.cpp（0x20 步长），数组指针在 GameSceneNode+0x1C0
	struct bone_joint_t
	{
		sdk::vector pos;
		float scale;
		char pad[0x10];
	};
	static_assert(sizeof(bone_joint_t) == 0x20, "bone joint stride mismatch");

	// 整条骨骼读取链 SEH 兜底（pawn 可能悬垂、偏移可能过期）；读出 0..k_bone_max
	__declspec(noinline) bool read_bones_seh(sdk::cs2_player_pawn* pawn, sdk::vector out[k_bone_max + 1])
	{
		__try
		{
			const auto node = pawn->get_m_pGameSceneNode();
			if (!node)
				return false;
			const auto joints = *reinterpret_cast<bone_joint_t**>(reinterpret_cast<uint8_t*>(node) + 0x1C0);
			if (!joints)
				return false;
			for (auto i = 0u; i <= k_bone_max; ++i)
				out[i] = joints[i].pos;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// 本地眼位读取：与 visuals::read_local_eye_seh 同链
	__declspec(noinline) bool read_local_eye_seh(sdk::vector* eye)
	{
		__try
		{
			const auto ctrl = sdk::LocalPlayerController;
			if (!ctrl)
				return false;
			const auto pawn = ctrl->get_pawn();
			if (!pawn)
				return false;
			const auto node = pawn->get_m_pGameSceneNode();
			if (!node)
				return false;
			*eye = node->get_vec_abs_origin();
			eye->z += pawn->get_m_vecViewOffset().get_vec_z();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// [hitchance] 本地 2D 速度读取（同 read_local_eye_seh 链，SEH 兜底）
	__declspec(noinline) bool read_local_velocity_seh(float* vel)
	{
		__try
		{
			const auto ctrl = sdk::LocalPlayerController;
			if (!ctrl)
				return false;
			const auto pawn = ctrl->get_pawn();
			if (!pawn)
				return false;
			*vel = pawn->get_m_vecAbsVelocity().length_2d();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void calc_angle(const sdk::vector& from, const sdk::vector& to, float* pitch, float* yaw)
	{
		const auto dx = to.x - from.x;
		const auto dy = to.y - from.y;
		const auto dz = to.z - from.z;
		const auto hyp = std::sqrt(dx * dx + dy * dy);
		*yaw = std::atan2(dy, dx) * k_rad2deg;
		*pitch = std::atan2(-dz, hyp) * k_rad2deg;
	}

	float angle_diff(const float a, const float b)
	{
		auto d = std::fmod(a - b, 360.f);
		if (d > 180.f)
			d -= 360.f;
		if (d < -180.f)
			d += 360.f;
		return d;
	}

	// [aim] 视角槽 build 14188 实测偏移：CCSGOInput+0x688（三帧自扫描实锤）
	__declspec(noinline) bool aim_read_view(const void* input, float* p, float* y, float* r)
	{
		__try
		{
			const auto va = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(input)
				+ sdk::offsets::ccsgo_input::view_angles);
			*p = va[0];
			*y = va[1];
			*r = va[2];
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	__declspec(noinline) bool aim_write_view(void* input, float p, float y)
	{
		__try
		{
			const auto va = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(input)
				+ sdk::offsets::ccsgo_input::view_angles);
			va[0] = p;
			va[1] = y;
			va[2] = 0.f;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// [weapon-group] 动态解析当前武器组 ID：读本地 pawn → weapon_services → active_weapon
	// → item_definition_index，然后在 menu::group::grp_to_wep 反向查组。失败回落 general_id=512。
	// SEH 只包读取链（必须是 no C++ 对象的扁平函数），std::find 在 __try 外。
	__declspec(noinline) bool read_local_weapon_id_seh(uint32_t* out_id)
	{
		__try
		{
			const auto ctrl = sdk::LocalPlayerController;
			if (!ctrl)
				return false;
			const auto pawn = ctrl->get_pawn();
			if (!pawn)
				return false;
			const auto ws = pawn->get_weapon_services_ptr();
			if (!ws)
				return false;
			const auto wpn = ws->get_h_active_weapon().get();
			if (!wpn)
				return false;
			*out_id = wpn->get_attribute_manager().get_item().get_item_definition_index();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// [weapon-group] 非插入式读取 cfgs[slot]。arr_setting::operator[] 缺键会插入默认值
	// （bool=false / float=0），若每帧调用会污染配置表，并与菜单线程的 rebuild 迭代冲突。
	bool cfg_slot_enabled(evo::gui::value_param_array<bool>& arr, const uint32_t slot)
	{
		for (auto it = arr.begin(); it != arr.end(); ++it)
			if (it->first == slot)
				return it->second.get();
		return false;
	}

	// [weapon-group] 运行时配置槽解析，优先级与 derive_weapon_setting 一致：
	//   ① 单武器 override（菜单 Add override 里勾了具体枪）→ 该武器 ID
	//   ② 所属组 override（勾了 Rifles 等组）→ 组 ID
	//   ③ 都没有 → General（general_id=512）
	// 注意：General → 组/武器是「复制」语义（derive_weapon_setting 在勾选 override 时
	// copy 一份），之后各槽独立，改 General 不会回溯影响已启用的组。
	uint32_t resolve_weapon_group_id()
	{
		uint32_t id = 0;
		if (!read_local_weapon_id_seh(&id))
			return menu::group::general_id;

		auto& cfgs = cfg.weapon_config.cfgs;
		if (cfg_slot_enabled(cfgs, id))
			return id;

		for (const auto& [grp, weps] : menu::group::grp_to_wep)
			if (std::find(weps.begin(), weps.end(), id) != weps.end() && cfg_slot_enabled(cfgs, grp))
				return grp;

		return menu::group::general_id;
	}

}

void aimbot_t::run(sdk::ccsgo_input* input)
{
	// [weapon-group] 每帧解析一次，供 hitboxes / hitchance / autostop 等读取
	const auto cur_grp = resolve_weapon_group_id();
	

	silent_valid = false;
	wants_autofire = false;
	wants_autostop = false;
	// [visualize] 每帧先清（早期 return 分支如菜单打开/取视角失败时不留上帧残影）
	vis_valid = false;

	if (!cfg.aim.enabled.get() || !input)
		return;

	if (menu::men.is_open())
		return;

	float cur_p = 0.f, cur_y = 0.f, cur_r = 0.f;
	if (!aim_read_view(input, &cur_p, &cur_y, &cur_r))
		return;

	sdk::vector eye{};
	if (!read_local_eye_seh(&eye))
		return;

	const auto max_fov = cfg.aim.fov.get();

	// [hitbox 多点] 菜单 Rage > Hitboxes 多选（仅 general 组生效，id=menu::general_id=512）。
	// 按致死率/高度构建本 tick 瞄准点优先级：头→颈→胸→腹→臂→腿→脚。
	// 勾选的组才会产生 trace（头可见时永远只打 1 条）；一个都没勾 → 回落纯打头。
	struct aim_pt { uint32_t bone; };
	aim_pt pts[20];
	auto pn = 0;
	const auto hb = static_cast<uint64_t>(cfg.weapon_config.hitboxes[cur_grp].get());
	const auto hb_on = [&](const uint32_t bit) { return (hb & (1ull << bit)) != 0; };
	const auto headshot_only = cfg.aim.hsmode.get();
	if (headshot_only)
		pts[pn++].bone = k_bone_head;
	else if (hb_on(cfg_t::hitboxes_head))   pts[pn++].bone = k_bone_head;
	if (!headshot_only)
	{
		if (hb_on(cfg_t::hitboxes_neck))   pts[pn++].bone = k_bone_neck;
		if (hb_on(cfg_t::hitboxes_chest))  pts[pn++].bone = k_bone_spine2;
		if (hb_on(cfg_t::hitboxes_stomach))pts[pn++].bone = k_bone_spine1;
		if (hb_on(cfg_t::hitboxes_arms))
			for (auto b = k_bone_arm_first; b <= k_bone_arm_last && pn < 20; ++b) pts[pn++].bone = b;
		if (hb_on(cfg_t::hitboxes_legs))
			for (auto b = k_bone_leg_first; b <= k_bone_leg_last && pn < 20; ++b) pts[pn++].bone = b;
		if (hb_on(cfg_t::hitboxes_feet))
		{
			pts[pn++].bone = k_bone_foot_l;
			if (pn < 20) pts[pn++].bone = k_bone_foot_r;
		}
	}
	if (pn == 0)
		pts[pn++].bone = k_bone_head;

	// FOV 内候选按 fov 升序收集（fov 统一按头角算，代表"敌人离准星远近"）：
	// fov 最小的目标全身被挡时不再堵死选择（自动切到下一个可见目标）；
	// 锁定中的目标 fov≈0 恒排最前，天然保持粘性不抖动。
	struct aim_cand
	{
		sdk::cs2_player_pawn* pawn;
		float fov;
	};
	aim_cand cands[sdk::max_players];
	auto n = 0;

	for (auto i = 1; i <= sdk::max_players; ++i)
	{
		const auto ctrl = sdk::GameEntitySystem->get_player_controller(i);
		if (!ctrl || ctrl->get_is_local_player_controller())
			continue;
		const auto pawn = sdk::GameEntitySystem->get_player_pawn(i);
		if (!pawn || !pawn->is_alive() || !pawn->is_enemy())
			continue;

		sdk::vector bones[k_bone_max + 1]{};
		if (!read_bones_seh(pawn, bones))
			continue;
		const auto& head = bones[k_bone_head];

		float pitch = 0.f, yaw = 0.f;
		calc_angle(eye, head, &pitch, &yaw);
		const auto dp = angle_diff(pitch, cur_p);
		const auto dy = angle_diff(yaw, cur_y);
		const auto fov = std::sqrt(dp * dp + dy * dy);
		if (fov >= max_fov)
			continue;

		// 升序插入（候选最多几个，线性挪动即可）
		auto j = n;
		while (j > 0 && cands[j - 1].fov > fov)
		{
			cands[j] = cands[j - 1];
			--j;
		}
		cands[j].pawn = pawn;
		cands[j].fov = fov;
		++n;
	}

	// 目标 × 瞄准点 两级探测：从 fov 最小的敌人开始，其内部按头→脚优先级取第一个
	// 可见点；该敌人所有勾选点都被挡才换下一个敌人（头被掩体边缘挡住时改打身体）
	auto found = false;
	sdk::vector aim_pos{};
	// [visualize] 无可见点时仍记录「会被瞄准但打不到」的点（fov 最小候选的首个优先级点），
	// 供 visuals 层用 aim 色区分；否则 vis_safe 恒为 true，标记只有 safe 一种颜色
	auto blocked_found = false;
	sdk::vector blocked_pos{};
	for (auto i = 0; i < n && !found; ++i)
	{
		sdk::vector bones[k_bone_max + 1]{};
		if (!read_bones_seh(cands[i].pawn, bones))
			continue;

		if (!blocked_found)
		{
			blocked_pos = bones[pts[0].bone];
			blocked_found = true;
		}

		for (auto k = 0; k < pn; ++k)
		{
			const auto& pt = bones[pts[k].bone];
			sdk::vector dir = pt;
			dir.x -= eye.x;
			dir.y -= eye.y;
			dir.z -= eye.z;
			const float dist = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

			// [可见性] 贴脸敌人直接判可见（不可能有墙夹在中间，且避免自身碰撞盒把 trace 挡成 frac≈0.4/dist）
			bool visible;
			const void* hit_ent = nullptr;
			float frac = 1.f;
			if (dist < 1.5f)
			{
				visible = true;
			}
			else
			{
				// [可见性] trace 起点向前偏移 1.2m，确保完全跳出本地碰撞胶囊体（半径 ~0.81m）
				float t = 1.2f / dist;
				if (t > 0.95f) t = 0.95f;
				const float st[3] = {eye.x + dir.x * t, eye.y + dir.y * t, eye.z + dir.z * t};
				const float ed[3] = {pt.x, pt.y, pt.z};
				frac = hooks::client::aw_trace_query(st, ed, &hit_ent);

				// 无命中（frac≥0.99 且无实体）→ 可见；命中目标 pawn 自身 → 可见（骨骼点在
				// hitbox 内部）；其余（墙/别人）→ 换下一个瞄准点
				visible = (hit_ent == nullptr)
					? (frac >= 0.99f)
					: (hit_ent == cands[i].pawn);
			}

			if (!visible)
				continue;

			aim_pos = pt;
			found = true;
			break;
		}
	}

	if (found)
	{
		float pitch = 0.f, yaw = 0.f;
		calc_angle(eye, aim_pos, &pitch, &yaw);

		if (cfg.aim.silent.get())
		{
			// [silent] 不动 live 视角（本地画面无感），目标角交由 fill_cmd_angles
			// 钩写进本 tick 命令 → 服务端打选中的骨骼点
			silent_pitch = pitch;
			silent_yaw = yaw;
			silent_valid = true;
		}
		else
		{
			// v1 直写 live 视角槽：非 silent，准星每 tick snap 到目标点（先验证管线）
			aim_write_view(input, pitch, yaw);
		}
	}

	// [hitchance] 门控 autofire：hc=0 不门控；hc=100 要求 2D 速度 ≤ 2 u/s（完全停稳）。
	// 线性映射阈值 250 - hc*2.48（250 = 步枪满速）。读不到速度时不射击（保守）。
	// 读当前武器组配置（动态解析，回落到 general_id=512）。
	wants_autofire = false;
	if (found && cfg.aim.autofire.get())
	{
		const auto hc = cfg.weapon_config.hitchance[cur_grp].get();
		if (hc <= 0.f)
		{
			wants_autofire = true;
		}
		else
		{
			float vel = 0.f;
			const auto threshold = 250.f - hc * 2.48f;
			wants_autofire = read_local_velocity_seh(&vel) && vel <= threshold;
		}
	}
	// [autostop] 锁定目标时请求停止移动（读当前武器组配置）
	wants_autostop = found && cfg.weapon_config.autostop[cur_grp].get();

	// [visualize] 快照：可见 → 实际瞄准点用 safe 色；全部被挡 → 记录候选点用 aim 色
	vis_pos = found ? aim_pos : blocked_pos;
	vis_safe = found;
	vis_valid = found || blocked_found;
}
/* 
void aimbot_t::scale_damage(const int hitgroup,  sdk::cs2_base_entity* entity)
{
	data_t& data = m_data;

	// ida: server.dll; 80 78 42 00 74 08 F3 0F 59 35 ?? ?? ?? ?? 80 BE 04 0D 00 00 00
	sdk::convar* mp_damage_scale_ct_head = sdk::Cvar->find("mp_damage_scale_ct_head");
	if (!mp_damage_scale_ct_head || mp_damage_scale_ct_head->type != 7) return; // 7 = float
	float damage_scale_ct_head = mp_damage_scale_ct_head->value.fl;

	sdk::convar* mp_damage_scale_t_head = sdk::Cvar->find("mp_damage_scale_t_head");
	if (!mp_damage_scale_t_head || mp_damage_scale_t_head->type != 7) return;
	float damage_scale_t_head = mp_damage_scale_t_head->value.fl;

	sdk::convar* mp_damage_scale_ct_body = sdk::Cvar->find("mp_damage_scale_ct_body");
	if (!mp_damage_scale_ct_body || mp_damage_scale_ct_body->type != 7) return;
	float damage_scale_ct_body = mp_damage_scale_ct_body->value.fl;

	sdk::convar* mp_damage_scale_t_body = sdk::Cvar->find("mp_damage_scale_t_body");
	if (!mp_damage_scale_t_body || mp_damage_scale_t_body->type != 7) return;
	float damage_scale_t_body = mp_damage_scale_t_body->value.fl;

	

	const bool is_ct = entity->get_m_iTeamNum() == 3, is_t = entity->get_m_iTeamNum() == 2;

	float head_damage_scale = is_ct ? damage_scale_ct_head : is_t ? damage_scale_t_head : 1.0f;
	const float body_damage_scale = is_ct ? damage_scale_ct_body : is_t ? damage_scale_t_body : 1.0f;

	// william: magic values u can see here: ida: server.dll; F3 0F 10 35 ?? ?? ?? ?? 0F 29 7C 24 30 44 0F 29 44 24
	// xref: mp_heavybot_damage_reduction_scale 
	//if (entity->get_m_bHasHeavyArmor()) { //Not considering 
	//	head_damage_scale *= 0.5f;
	//}

	// todo: 可通过m_flHeadshotMultiplier 偏移获取
	switch (hitgroup) {
	case k_bone_head:
		data.m_dmg *= data.m_wpn_data->m_flHeadshotMultiplier() * head_damage_scale;
		break;
	case game_sdk::entity::e_hitgroups::hitgroup_chest:
	case game_sdk::entity::e_hitgroups::hitgroup_left_arm:
	case game_sdk::entity::e_hitgroups::hitgroup_right_arm:
	case k_bone_neck:
		data.m_dmg *= body_damage_scale;
		break;
	case game_sdk::entity::e_hitgroups::hitgroup_stomach:
		data.m_dmg *= 1.25f * body_damage_scale;
		break;
	case game_sdk::entity::e_hitgroups::hitgroup_left_leg:
	case game_sdk::entity::e_hitgroups::hitgroup_right_leg:
		data.m_dmg *= 0.75f * body_damage_scale;
		break;
	default:
		break;
	}

	if (!entity->is_armored(hitgroup))
		return;

	float heavy_armor_bonus = 1.0f, armor_bonus = 0.5f, armor_ratio = data.m_wpn_data->m_flArmorRatio() * 0.5f;

	//if (entity->m_bHasHeavyArmor()) { //无需考虑重装甲
	//	heavy_armor_bonus = 0.25f;
	//	armor_bonus = 0.33f;
	//	armor_ratio *= 0.20f;
	//}

	float damage_to_health = data.m_dmg * armor_ratio;
	const float damage_to_armor = (data.m_dmg - damage_to_health) * (heavy_armor_bonus * armor_bonus);

	if (damage_to_armor > static_cast<float>(entity->m_ArmorValue())) {
		damage_to_health = data.m_dmg - static_cast<float>(entity->m_ArmorValue()) / armor_bonus;
	}

	data.m_dmg = damage_to_health;
}
*/
/* 
bool c_auto_wall::fire_bullet() {
	data_t& data = m_data;
	CS2_ASSERT(data.m_local != nullptr || data.m_target != nullptr || data.m_wpn_data != nullptr);

	game_sdk::trace_data_t trace_data{ };
	trace_data.m_arr_pointer = &trace_data.m_arr;

	const utilities::vec3_t direction =
		data.m_pos.at(data_t::e_pos::e_target) - data.m_pos.at(data_t::e_pos::e_local),
		end_pos = direction * data.m_wpn_data->m_flRange();

	const game_sdk::trace_filter_t filter(game_sdk::k_main_pen_mask, data.m_local, 3);
	create_trace(&trace_data, data.m_pos.at(data_t::e_pos::e_local), end_pos, filter, 4);

	struct handle_bullet_data_t {
		handle_bullet_data_t(const float dmg_mod, const float pen, const float range_mod, const float range,
			const int pen_count, const bool failed) :
			m_dmg(dmg_mod),
			m_pen(pen),
			m_range_mod(range_mod),
			m_range(range),
			m_pen_count(pen_count),
			m_failed(failed) {
		}

		float m_dmg{ }, m_pen{ }, m_range_mod{ }, m_range{ };
		int m_pen_count{ };
		bool m_failed{ };
	} handle_bullet_data(static_cast<float>(data.m_wpn_data->m_nDamage()),
		data.m_wpn_data->m_flPenetration(), data.m_wpn_data->m_flRange(),
		data.m_wpn_data->m_flRangeModifier(), 4, false);
	if (trace_data.m_num_update > 0) {
		for (int i{ }; i < trace_data.m_num_update; i++) {
			auto* value = reinterpret_cast<game_sdk::update_value_t* const>(
				reinterpret_cast<std::uintptr_t>(trace_data.m_pointer_update_value)
				+ i * sizeof(game_sdk::update_value_t));

			game_sdk::game_trace_t game_trace{ };
			init_trace_info(&game_trace);
			get_trace_info(
				&trace_data, &game_trace, 0.0f,
				reinterpret_cast<void*>(
					reinterpret_cast<std::uintptr_t>(trace_data.m_arr.data())
					+ sizeof(game_sdk::trace_arr_element_t) * (value->m_handle_idx & game_sdk::entity::k_ent_entry_mask)));

			if (game_trace.m_hit_entity == data.m_target) {
				if (data.m_dmg > 0.0f) // huh?
					scale_damage(game_trace.m_hitbox_data->m_hitgroup, game_trace.m_hit_entity);
				return true;
			}

			if (handle_bullet_penetration(&trace_data, &handle_bullet_data, value))
				return false;

			data.m_dmg = handle_bullet_data.m_dmg;
		}
	}

	return false;
}
*/

// ---------------------------------------------------------------------------
// [anti-aim] Rage > Anti-aim > Angles backend
// Only rewrites the "real" angles sent to the server (CUserCmd +0x18 pitch / +0x1C yaw).
// The local camera is untouched. Silent aim wins on frames where we actually shoot.
// No desync / fake yaw / fakelag here: those need subtick dual-angle writes.
namespace antiaim
{
	static float normalize_yaw(float yaw)
	{
		while (yaw > 180.f)
			yaw -= 360.f;
		while (yaw < -180.f)
			yaw += 360.f;
		return yaw;
	}

	bool apply(float& pitch, float& yaw)
	{
		if (!cfg.aa.enabled.get())
			return false;

		// create_move runs once per tick, used for jitter side flipping
		static uint32_t s_tick = 0;
		++s_tick;

		const float base_yaw = yaw;
		float out_yaw = base_yaw;

		// manual override (left/right/back) takes priority over automatic yaw
		if (cfg.aa.manual.get())
		{
			if (cfg.aa.manual_back.get())
				out_yaw = base_yaw + 180.f;
			else if (cfg.aa.manual_left.get())
				out_yaw = base_yaw - 90.f;
			else if (cfg.aa.manual_right.get())
				out_yaw = base_yaw + 90.f;
		}
		else if (cfg.aa.yaw->test(cfg_t::aa_yaw_backwards))
			out_yaw = base_yaw + 180.f;
		else if (cfg.aa.yaw->test(cfg_t::aa_yaw_custom))
			out_yaw = base_yaw + cfg.aa.yaw_value.get();

		const float jitter = cfg.aa.yaw_jitter_value.get();
		if (jitter != 0.f)
		{
			if (cfg.aa.yaw_jitter->test(cfg_t::aa_yaw_jitter_center))
				out_yaw += (s_tick & 1u) ? jitter * 0.5f : -jitter * 0.5f;
			else if (cfg.aa.yaw_jitter->test(cfg_t::aa_yaw_jitter_offset))
				out_yaw += (s_tick & 1u) ? jitter : -jitter;
		}

		// pitch: +89 = look down (engine limit), -89 = look up
		if (cfg.aa.pitch->test(cfg_t::aa_pitch_down))
			pitch = 89.f;
		else if (cfg.aa.pitch->test(cfg_t::aa_pitch_up))
			pitch = -89.f;
		else if (cfg.aa.pitch->test(cfg_t::aa_pitch_zero))
			pitch = 0.f;
		else if (cfg.aa.pitch->test(cfg_t::aa_pitch_custom))
		{
			pitch = cfg.aa.pitch_value.get();
			pitch = pitch > 89.f ? 89.f : (pitch < -89.f ? -89.f : pitch);
		}

		yaw = normalize_yaw(out_yaw);
		return true;
	}
}