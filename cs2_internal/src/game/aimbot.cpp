#include <game/aimbot.h>
#include <game/cfg.h>
#include <game/game.h>
#include <hooks/hooks.h>
#include <menu/menu.h>
#include <sdk/input.h>
#include <sdk/offsets.h>
#include <sdk/sdk.h>
#include <windows.h>
#include <cmath>
#include <cstdio>

// [silent] 本 tick 目标角（定义见 aimbot.h 注释）
bool aimbot_t::silent_valid = false;
float aimbot_t::silent_pitch = 0.f;
float aimbot_t::silent_yaw = 0.f;

namespace
{
	// [aim-diag 已删] 64 tick 滚动统计（calls/nocmd/noeye/enemies/found/fired）——
	// 管线已验证稳定（locks=128/128、autofire 实测成功），死计数器不再保留

	// 骨骼索引表（2026-04 animgraph 后）：
	// pelvis=1/spine_1=2/spine_2=4/neck=6/head=7/臂9-15/腿17-22（root_motion 占 0）
	// —— 与 visuals 骨架同源
	constexpr uint32_t k_bone_pelvis = 1;
	constexpr uint32_t k_bone_spine1 = 2;
	constexpr uint32_t k_bone_spine2 = 4;
	constexpr uint32_t k_bone_neck = 6;
	constexpr uint32_t k_bone_head = 7;
	constexpr uint32_t k_bone_arm_first = 9;
	constexpr uint32_t k_bone_arm_last = 15;
	constexpr uint32_t k_bone_leg_first = 17;
	constexpr uint32_t k_bone_leg_last = 20;
	constexpr uint32_t k_bone_foot_l = 21;
	constexpr uint32_t k_bone_foot_r = 22;
	constexpr uint32_t k_bone_max = 22;
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

	// [btn 扫描机器已删] v1/v2/v3 三连败（a0 头 64KB changed=0、+0x61E0/+0x6330 恒 null、
	// 24 条堆指针链全配对零变化）→ 按钮态在 create_move 内部每帧命令对象，静态不可达。
	// 务实替代 = 下方 SendInput 合成点击（锁头已物理对准目标，游戏自身走 IN_ATTACK→开火）。

	// [autofire] SendInput 左键「按住镜像」：按住态严格镜像本 tick 可见性——
	// 可见目标 → DOWN 并持续按住（连发由游戏武器自身 refire 节流，射速精准）；
	// 目标丢失/被墙挡/菜单开/开关关 → 立即 UP。
	// 注意：DOWN+UP 不能同批注入——游戏输入泵把同消息循环内的瞬时按压丢弃，不开火
	// （已实测）。SendInput 事件下一帧才被泵消费，架构上 UP 晚一帧生效=最多多 1 发，
	// 彻底消除要靠 IN_ATTACK 命令位直写。
	bool g_click_held = false;

	void syn_click_down()
	{
		INPUT in{};
		in.type = INPUT_MOUSE;
		in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		SendInput(1, &in, sizeof(in));
	}

	void syn_click_up()
	{
		INPUT in{};
		in.type = INPUT_MOUSE;
		in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
		SendInput(1, &in, sizeof(in));
		g_click_held = false;
	}
}

void aimbot_t::run(sdk::ccsgo_input* input)
{
	silent_valid = false; // 无新目标时让 fill_cmd_angles 钩回落原版行为

	if (!cfg.aim.enabled.get() || !input)
	{
		if (g_click_held)
			syn_click_up(); // 关开关/无输入：确保不残留合成按住
		return;
	}

	// 菜单打开时冻结（不锁人不合成点击），松掉合成按住
	if (menu::men.is_open())
	{
		if (g_click_held)
			syn_click_up();
		return;
	}

	float cur_p = 0.f, cur_y = 0.f, cur_r = 0.f;
	if (!aim_read_view(input, &cur_p, &cur_y, &cur_r))
	{
		if (g_click_held)
			syn_click_up();
		return;
	}

	sdk::vector eye{};
	if (!read_local_eye_seh(&eye))
	{
		if (g_click_held)
			syn_click_up();
		return;
	}

	const auto max_fov = cfg.aim.fov.get();

	// [hitbox 多点] 菜单 Rage > Hitboxes 多选（仅 general 组生效，id=menu::general_id=512）。
	// 按致死率/高度构建本 tick 瞄准点优先级：头→颈→胸→腹→臂→腿→脚。
	// 勾选的组才会产生 trace（头可见时永远只打 1 条）；一个都没勾 → 回落纯打头。
	struct aim_pt { uint32_t bone; };
	aim_pt pts[20];
	auto pn = 0;
	const auto hb = static_cast<uint64_t>(cfg.weapon_config.hitboxes[512].get());
	const auto hb_on = [&](const uint32_t bit) { return (hb & (1ull << bit)) != 0; };
	if (hb_on(cfg_t::hitboxes_head))   pts[pn++].bone = k_bone_head;
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
	for (auto i = 0; i < n && !found; ++i)
	{
		sdk::vector bones[k_bone_max + 1]{};
		if (!read_bones_seh(cands[i].pawn, bones))
			continue;

		for (auto k = 0; k < pn; ++k)
		{
			const auto& pt = bones[pts[k].bone];
			const float st[3] = {eye.x, eye.y, eye.z};
			const float ed[3] = {pt.x, pt.y, pt.z};
			const void* hit_ent = nullptr;
			const auto frac = hooks::client::aw_trace_query(st, ed, &hit_ent);

			// 无命中（frac≥0.99 且无实体）→ 可见；命中目标 pawn 自身 → 可见（骨骼点在
			// hitbox 内部）；frac≤0.05 → 起点内命中（眼位在本地碰撞盒内部，trace 一出发
			// 撞自己：实测 frac=0.000~0.017 且朝向无关；真墙实测 frac≥0.38）→ 可见；
			// 其余（远墙/别人）→ 换下一个瞄准点
			const bool visible = (hit_ent == nullptr)
				? (frac >= 0.99f)
				: (hit_ent == cands[i].pawn || frac <= 0.05f);
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

	// [autofire] 按住镜像：可见且 autofire 开 → DOWN 一次并保持（连发由游戏武器
	// 自身 refire 节流）；目标丢失/被挡/关 autofire → 立即 UP。
	// 注意：user_lmb 只在「未按住」时判 DOWN 边沿——GetAsyncKeyState 读到的是含
	// 我们自己注入事件在内的异步状态，若按住期间也查它，下一帧就会把合成 DOWN
	// 当成"用户按压"立即松开，按压跨不过一条命令 = 永不开火（上版 bug）。
	const bool user_lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

	if (g_click_held)
	{
		if (!found || !cfg.aim.autofire.get())
			syn_click_up();
	}
	else if (found && cfg.aim.autofire.get() && !user_lmb)
	{
		syn_click_down();
		g_click_held = true;
	}
}
