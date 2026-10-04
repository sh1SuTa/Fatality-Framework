#include <utils/murmur2.h>
#include <cinttypes>
#include <game/cfg.h>
#include <game/draw_manager.h>
#include <game/game.h>
#include <game/visuals.h>
#include <sdk/client.h>
#include <sdk/engine.h>
#include <sdk/globalvars.h>
#include <sdk/localize.h>
#include <utils/math.h>
#include "tinyformat.h"


visuals_t visuals;

using namespace evo::ren;

namespace
{
	// 常见武器弹匣容量（weapon_id → mag），取自公开武器数据；未列出的返回 0（不绘制）。
	// maxclip 原通过游戏内部函数 fn.get_weapon_data 获取，但实测该偏移在当前 build
	// 指向错误函数，且失败方式为 fail-fast（__except 无法兜底，进程直接终止），
	// 故弃用函数调用，改为纯 schema 数据查表。
	constexpr std::pair<uint32_t, int> mag_sizes[] = {
		{sdk::weapon_deagle, 7}, {sdk::weapon_elite, 30}, {sdk::weapon_fiveseven, 20}, {sdk::weapon_glock, 20},
		{sdk::weapon_ak47, 30}, {sdk::weapon_aug, 30}, {sdk::weapon_awp, 10}, {sdk::weapon_famas, 25},
		{sdk::weapon_g3sg1, 20}, {sdk::weapon_galilar, 35}, {sdk::weapon_m249, 100}, {sdk::weapon_m4a1, 30},
		{sdk::weapon_mac10, 30}, {sdk::weapon_p90, 50}, {sdk::weapon_mp5sd, 30}, {sdk::weapon_ump45, 25},
		{sdk::weapon_xm1014, 7}, {sdk::weapon_bizon, 64}, {sdk::weapon_mag7, 5}, {sdk::weapon_negev, 150},
		{sdk::weapon_sawedoff, 7}, {sdk::weapon_tec9, 18}, {sdk::weapon_hkp2000, 12}, {sdk::weapon_mp7, 30},
		{sdk::weapon_mp9, 30}, {sdk::weapon_nova, 8}, {sdk::weapon_p250, 13}, {sdk::weapon_scar20, 20},
		{sdk::weapon_sg556, 30}, {sdk::weapon_ssg08, 10}, {sdk::weapon_m4a1_silencer, 20}, {sdk::weapon_usp_silencer, 12},
		{sdk::weapon_cz75a, 16}, {sdk::weapon_revolver, 8},
	};

	int lookup_mag_size(const uint32_t id)
	{
		for (const auto& entry : mag_sizes)
			if (entry.first == id)
				return entry.second;
		return 0;
	}

	// weapon_id → 显示名（同 mag_sizes 思路：替代已弃用的 get_weapon_data + localize 链路）；
	// 未列出的返回 nullptr（不显示）
	constexpr std::pair<uint32_t, const char*> weapon_names[] = {
		{sdk::weapon_deagle, "Desert Eagle"}, {sdk::weapon_elite, "Dual Berettas"}, {sdk::weapon_fiveseven, "Five-SeveN"},
		{sdk::weapon_glock, "Glock-18"}, {sdk::weapon_ak47, "AK-47"}, {sdk::weapon_aug, "AUG"}, {sdk::weapon_awp, "AWP"},
		{sdk::weapon_famas, "FAMAS"}, {sdk::weapon_g3sg1, "G3SG1"}, {sdk::weapon_galilar, "Galil AR"},
		{sdk::weapon_m249, "M249"}, {sdk::weapon_m4a1, "M4A4"}, {sdk::weapon_mac10, "MAC-10"}, {sdk::weapon_p90, "P90"},
		{sdk::weapon_mp5sd, "MP5-SD"}, {sdk::weapon_ump45, "UMP-45"}, {sdk::weapon_xm1014, "XM1014"},
		{sdk::weapon_bizon, "PP-Bizon"}, {sdk::weapon_mag7, "MAG-7"}, {sdk::weapon_negev, "Negev"},
		{sdk::weapon_sawedoff, "Sawed-Off"}, {sdk::weapon_tec9, "Tec-9"}, {sdk::weapon_hkp2000, "P2000"},
		{sdk::weapon_mp7, "MP7"}, {sdk::weapon_mp9, "MP9"}, {sdk::weapon_nova, "Nova"}, {sdk::weapon_p250, "P250"},
		{sdk::weapon_scar20, "SCAR-20"}, {sdk::weapon_sg556, "SG 553"}, {sdk::weapon_ssg08, "SSG 08"},
		{sdk::weapon_m4a1_silencer, "M4A1-S"}, {sdk::weapon_usp_silencer, "USP-S"}, {sdk::weapon_cz75a, "CZ75-Auto"},
		{sdk::weapon_revolver, "R8 Revolver"}, {sdk::weapon_taser, "Zeus x27"},
		{sdk::weapon_flashbang, "Flashbang"}, {sdk::weapon_hegrenade, "HE Grenade"}, {sdk::weapon_smokegrenade, "Smoke"},
		{sdk::weapon_molotov, "Molotov"}, {sdk::weapon_incgrenade, "Incendiary"}, {sdk::weapon_decoy, "Decoy"},
		{sdk::weapon_c4, "C4"}, {sdk::weapon_bayonet, "Bayonet"}, {sdk::weapon_knife, "Knife"},
		{sdk::weapon_knife_t, "Knife"}, {sdk::weapon_knife_karambit, "Karambit"}, {sdk::weapon_knife_m9bayonet, "M9 Bayonet"},
	};

	const char* lookup_weapon_name(const uint32_t id)
	{
		for (const auto& entry : weapon_names)
			if (entry.first == id)
				return entry.second;
		return nullptr;
	}

	// weapon_id → 图标短名（对应 assets/icons/equipment/<名>.svg，
	// 由 tools/vpk_extract.py 从 csgo/pak01_dir.vpk 提取）
	constexpr std::pair<uint32_t, const char*> weapon_icon_names[] = {
		{sdk::weapon_deagle, "deagle"}, {sdk::weapon_elite, "elite"}, {sdk::weapon_fiveseven, "fiveseven"},
		{sdk::weapon_glock, "glock"}, {sdk::weapon_ak47, "ak47"}, {sdk::weapon_aug, "aug"}, {sdk::weapon_awp, "awp"},
		{sdk::weapon_famas, "famas"}, {sdk::weapon_g3sg1, "g3sg1"}, {sdk::weapon_galilar, "galilar"},
		{sdk::weapon_m249, "m249"}, {sdk::weapon_m4a1, "m4a1"}, {sdk::weapon_mac10, "mac10"}, {sdk::weapon_p90, "p90"},
		{sdk::weapon_mp5sd, "mp5sd"}, {sdk::weapon_ump45, "ump45"}, {sdk::weapon_xm1014, "xm1014"},
		{sdk::weapon_bizon, "bizon"}, {sdk::weapon_mag7, "mag7"}, {sdk::weapon_negev, "negev"},
		{sdk::weapon_sawedoff, "sawedoff"}, {sdk::weapon_tec9, "tec9"}, {sdk::weapon_hkp2000, "hkp2000"},
		{sdk::weapon_mp7, "mp7"}, {sdk::weapon_mp9, "mp9"}, {sdk::weapon_nova, "nova"}, {sdk::weapon_p250, "p250"},
		{sdk::weapon_scar20, "scar20"}, {sdk::weapon_sg556, "sg556"}, {sdk::weapon_ssg08, "ssg08"},
		{sdk::weapon_m4a1_silencer, "m4a1_silencer"}, {sdk::weapon_usp_silencer, "usp_silencer"},
		{sdk::weapon_cz75a, "cz75a"}, {sdk::weapon_revolver, "revolver"}, {sdk::weapon_taser, "taser"},
		{sdk::weapon_flashbang, "flashbang"}, {sdk::weapon_hegrenade, "hegrenade"}, {sdk::weapon_smokegrenade, "smokegrenade"},
		{sdk::weapon_molotov, "molotov"}, {sdk::weapon_incgrenade, "incgrenade"}, {sdk::weapon_decoy, "decoy"},
		{sdk::weapon_c4, "c4"}, {sdk::weapon_bayonet, "bayonet"}, {sdk::weapon_knife, "knife"},
		{sdk::weapon_knife_t, "knife_t"}, {sdk::weapon_knife_karambit, "knife_karambit"}, {sdk::weapon_knife_m9bayonet, "knife_m9_bayonet"},
	};

	const char* lookup_weapon_icon(const uint32_t id)
	{
		for (const auto& entry : weapon_icon_names)
			if (entry.first == id)
				return entry.second;
		return nullptr;
	}

	// SEH 保护的主动武器定义索引读取（weapon_services → active_weapon → econ item）
	__declspec(noinline) bool read_weapon_id_seh(sdk::cs2_player_pawn* pawn, uint32_t* id)
	{
		__try
		{
			const auto ws = pawn->get_weapon_services_ptr();
			if (!ws)
				return false;

			const auto wpn = ws->get_h_active_weapon().get();
			if (!wpn)
				return false;

			*id = wpn->get_attribute_manager().get_item().get_item_definition_index();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// SEH 保护的弹药读取：clip 与武器定义索引均为 schema 字段，
	// maxclip 走 mag_sizes 查表，全程无游戏内部函数调用
	__declspec(noinline) bool read_ammo_seh(sdk::cs2_player_pawn* pawn, int* clip, int* maxclip)
	{
		__try
		{
			const auto ws = pawn->get_weapon_services_ptr();
			if (!ws)
				return false;

			const auto wpn = ws->get_h_active_weapon().get();
			if (!wpn)
				return false;

			*clip = clamp(wpn->get_clip1(), 0, 1000);
			if (*clip <= 0)
				return false;

			*maxclip = lookup_mag_size(wpn->get_attribute_manager().get_item().get_item_definition_index());
			return *maxclip > 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// 骨骼关节布局：与外部实测实现（AimStar a2x Bone.h）一致，步长 0x20
	struct bone_joint_t
	{
		sdk::vector pos; // +0x00 世界坐标
		float scale; // +0x0C
		char pad[0x10];
	};
	static_assert(sizeof(bone_joint_t) == 0x20, "bone joint stride mismatch");

	// 骨骼数组指针位于 CSkeletonInstance + 0x1C0（model_state 0x140 + CModelState 内部指针 0x80）。
	// 该字段不属于网络 schema，偏移取自同 build 外部实测（a2x Offsets.h: BoneArray = 0x140 + 0x80）。
	// 整条读取链 SEH 兜底，偏移失效时返回 false 而不是崩溃。
	__declspec(noinline) bool read_bones_seh(sdk::cs2_player_pawn* pawn, sdk::vector* out, const uint32_t count)
	{
		__try
		{
			const auto node = pawn->get_m_pGameSceneNode();
			if (!node)
				return false;

			const auto joints = *reinterpret_cast<bone_joint_t**>(reinterpret_cast<uint8_t*>(node) + 0x1C0);
			if (!joints)
				return false;

			for (auto i = 0u; i < count; ++i)
				out[i] = joints[i].pos;

			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// world_to_screen 输出 NDC [-1,1]，统一在此转成屏幕像素
	evo::ren::vec2 ndc_to_px(const evo::ren::vec2& ndc)
	{
		const auto size = draw.display;
		return { (ndc.x + 1.f) * 0.5f * size.x, (1.f - ndc.y) * 0.5f * size.y };
	}

	// SEH 保护的本地眼位/视向读取：LocalPlayerController->get_pawn() 返回的 pawn
	// 可能在场景切换瞬间悬垂，字段裸读必须兜底
	__declspec(noinline) bool read_local_eye_seh(sdk::vector* eye, float* yaw)
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
			*yaw = pawn->get_v_angle().y;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

void visuals_t::run()
{
	if (!sdk::GameEntitySystem)
		return;

	// 本地玩家控制器每帧刷新：主菜单里为空，跨局会变，初始化读一次不够
	if (const auto client_base = static_cast<const uint8_t*>(MEM::GetModuleBaseHandle(CLIENT_DLL)))
		sdk::LocalPlayerController = *reinterpret_cast<sdk::cs2_player_controller* const*>(
			client_base + sdk::offsets::globals::local_player_controller);

	// 安全的进局判定：有效的本地玩家控制器只在局内存在，
	// 避免依赖未验证的引擎虚表槽位（se_call_vfunc 31/32/33 在 CS2 读到垃圾值，已弃用）
	if (!sdk::LocalPlayerController)
		return;

	auto view_setup = viewset;

	const auto last_ent = sdk::GameEntitySystem->get_last_entity_index();

	/*if (cfg.visuals.esp.world.enabled.get())
		for (auto i =  (sdk::max_players + 1); i <= last_ent; i++)
		{
			const auto entity = sdk::GameEntitySystem ->get_entity_by_index(i);
			if (!entity || !entity->get_game_scene_node_ptr() || entity->get_h_owner_entity().get())
				continue;

			if (!entity->get_entity_ptr()->get_designer_name() || *entity->get_entity_ptr()->get_designer_name() == '0')
				continue;

			const auto hashed_name = utils::fnv1a(entity->get_entity_ptr()->get_designer_name());
			if (!sdk::name_to_id.contains(hashed_name))
				continue;

			auto origin = *entity->get_computed_abs_origin();

			const auto dist = view_setup->origin.dist(origin);
			const auto cl_dist = clamp(dist - 500.f, 0.f, 500.f);
			draw_mgr.buf->g.alpha = (255.f - cl_dist / 2) / 255.f;
			if (draw_mgr.buf->g.alpha == 0.f)
				continue;

			const auto weapon_id = sdk::name_to_id[hashed_name];
			const auto name = std::string(entity->get_entity_ptr()->get_designer_name());

			const auto weapon = entity->as<sdk::schema::base_player_weapon>();
			const auto wpn_item = weapon ? &weapon->get_attribute_manager().get_item() : nullptr;

			vec2 pos{};
			if (!math::world_to_screen(origin, pos))
				continue;

			if (wpn_item && wpn_item->as<sdk::cs2_econ_item_view>()->get_item_definition())
			{
				auto offset = vec2{0, 7};
				if (cfg.visuals.esp.world.weapon_icons.get())
				{
					draw_mgr.add_icon(
						pos - offset, cfg.visuals.esp.world.color.get(), draw_mgr.get_panorama_texture(XOR("icons/equipment/") + name.substr(7), 12),
						text_params::with_vh(align_center, align_center));
				}
				else
				{
					draw_mgr.buf->font = draw.fonts[FNV1A("esp")];
					draw_mgr.buf->add_text(
						pos + offset, game->loc->find_safe(wpn_item->as<sdk::cs2_econ_item_view>()->get_item_definition()->item_type_name),
						cfg.visuals.esp.world.color.get(), text_params::with_vh(align_center, align_center));
				}

				const auto clip = clamp(weapon->get_clip1(), 0, 1000);
				const auto maxclip = clamp(game->fn.get_weapon_data(&weapon->get_attribute_manager().get_item())->get_max_clip1(), 0, 1000);

				if (clip > 0 && maxclip > 0)
					draw_bar(
						vec2{pos.x - 28, pos.y + 3} + offset, 52, true, {cfg.visuals.esp.world.color.get()}, static_cast<float>(clip) / maxclip,
						static_cast<float>(clip) / maxclip <= 0.93f ? std::optional(clip) : std::nullopt);
			}
		}*/

	for (auto i = 1; i <= sdk::max_players; i++)
	{
		auto& player = players[i];
		auto remove = [&](const bool force = false)
			{
				player.alpha -= 255.f / 0.3f * sdk::GlobalVars->frame_time;
				player.alpha = clamp(player.alpha, 0.f, 255.f);
				player.model_alpha -= 255.f / 0.3f * sdk::GlobalVars->frame_time;
				player.model_alpha = clamp(player.model_alpha, 0.f, 255.f);
				player.health_interpolated = 0.f;
				if (cfg.visuals.disablers->test(cfg_t::vis_disable_model_fade) || cfg.visuals.disablers->test(cfg_t::vis_disable_esp_fade))
					player.alpha = player.model_alpha = 0.f;

				if (force)
				{
					player.model_alpha = 0.f;
					player.alpha = 0.f;
				}

				player.alpha_lerp = player.alpha;
			};

		const auto player_controller = sdk::GameEntitySystem->get_player_controller(i);
		if (!player_controller || player_controller->get_is_local_player_controller())
		{
			remove(true);
			continue;
		}

		const auto player_pawn = sdk::GameEntitySystem->get_player_pawn(i);
		if (!player_pawn || !player_pawn->is_alive())
		{
			remove(true);
			continue;
		}

		if (player_pawn->get_m_flLastSpawnTimeIndex().get_value() != player.spawntime)
		{
			player.spawntime = player_pawn->get_m_flLastSpawnTimeIndex().get_value();
			player.alpha = 0.f;
			player.alpha_lerp = 0.f;
			player.model_alpha = 0.f;
		}

		player.model_alpha += 255.f / 0.5f * sdk::GlobalVars->frame_time;
		player.model_alpha = clamp(player.model_alpha, 0.f, 255.f);
		player.alpha_lerp += 255.f / 0.6f * sdk::GlobalVars->frame_time;
		player.alpha_lerp = clamp(player.alpha_lerp, 0.f, 255.f);

		if (cfg.visuals.disablers->test(cfg_t::vis_disable_model_fade))
			player.model_alpha = 255.f;

		if (cfg.visuals.disablers->test(cfg_t::vis_disable_esp_fade))
			player.alpha_lerp = 255.f;

		if (cfg.visuals.disablers->test(cfg_t::vis_disable_health_bar_interp) || static_cast<float>(player_pawn->get_m_iHealth()) > player.health_interpolated)
			player.health_interpolated = static_cast<float>(player_pawn->get_m_iHealth());

		player.health_interpolated = math::approach(
			static_cast<float>(player_pawn->get_m_iHealth()), player.health_interpolated, sdk::GlobalVars->frame_time * 23.f * std::max(
				1.f, player.health_interpolated - static_cast<float>(player_pawn->get_m_iHealth())));

		player.alpha = player.alpha_lerp;

		if (!player_pawn->is_enemy())
		{
			player.alpha = 0.f;
			player.alpha_lerp = 0.f;
			continue;
		}

		player.offset = {};

		player.pos = player_pawn->as<sdk::cs2_base_entity>()->get_computed_abs_origin();
		auto pos_top = player.pos + sdk::vector{ 0, 0, player_pawn->get_m_vecViewOffset().get_vec_z() + 8 };
		auto pos_bot = player.pos;
		pos_bot.z -= 4;

		const auto size = draw.display;

		// world_to_screen 输出 NDC [-1,1]，这里转成屏幕像素；
		// 之前整段被注释，top/bot 从不更新，ESP 全画在原点附近
		player.oof = false;
		evo::ren::vec2 s_top{}, s_bot{};
		if (!math::world_to_screen(pos_top, s_top))
			player.oof = true;
		if (!math::world_to_screen(pos_bot, s_bot))
			player.oof = true;
		player.top = { (s_top.x + 1.f) * 0.5f * size.x, (1.f - s_top.y) * 0.5f * size.y };
		player.bot = { (s_bot.x + 1.f) * 0.5f * size.x, (1.f - s_bot.y) * 0.5f * size.y };

		player.bot = player.bot.round();
		player.top = player.top.round();

		player.height = round(clamp(player.bot.y - player.top.y, 10.f, clamp(size.y * 1.5f, 10.f, 999999.f)));
		player.width = round(clamp(player.height / 3.8f, 3.f, size.x / 4.f));
		player.top = player.bot;
		player.top.y -= player.height;

		if (player.bot.x + player.width + 20 < 0 || player.bot.x - player.width - 20 > size.x || player.bot.y + 20 < 0 || player.bot.y - player.height - 20 > size.y)
			player.oof = true;

		auto& layer = draw_mgr.buf;
		layer->g.anti_alias = false;

		auto player_box = [&]()
			{
				if (!cfg.visuals.esp.box.get())
					return;

				const auto color = cfg.visuals.esp.box_color.get();
				layer->add_rect(rect(vec2{ player.top.x - player.width + 1.f, player.top.y + 1 }).size(vec2{ player.width * 2 - 2, player.height - 2 }), color);
				layer->add_rect(
					rect(vec2{ player.top.x - player.width, player.top.y }).size(vec2{ player.width * 2, static_cast<float>(player.height) }), ::color(color::black(), 0.4f));
				layer->add_rect(
					rect(vec2{ player.top.x - player.width + 2, player.top.y + 2 }).size(vec2{ player.width * 2 - 4, player.height - 4 }), ::color(color::black(), 0.4f));
			};

		auto player_info = [&]()
			{
				if (cfg.visuals.esp.armor.get())
				{
					const auto armor = player_pawn->get_m_ArmorValue();
					if (armor > 0)
						add_bar(player, esp_item_pos::bottom, { cfg.visuals.esp.armor_color.get() }, armor / 100.f, armor <= 93 ? std::optional(armor) : std::nullopt);
				}

				if (cfg.visuals.esp.ammo.get())
				{
					int clip = 0, maxclip = 0;
					if (read_ammo_seh(player_pawn, &clip, &maxclip))
					{
						// [dbg 已静音] 一次性面包屑「ammo read ok」——ammo 读取链已稳定
						add_bar(
							player, esp_item_pos::bottom, { cfg.visuals.esp.ammo_color.get() }, static_cast<float>(clip) / maxclip,
							static_cast<float>(clip) / maxclip <= 0.93f ? std::optional(clip) : std::nullopt);
					}
				}

				if (cfg.visuals.esp.health.get())
				{
					const auto health = player_pawn->get_m_iHealth();
					const auto max_health = std::max(health, player_pawn->get_m_iMaxHealth());
					std::vector<color> colors;
					if (cfg.visuals.esp.health_style->test(cfg_t::healthbar_solid))
						colors = { color::interpolate(cfg.visuals.esp.health_color_1.get(), cfg.visuals.esp.health_color_2.get(), player.health_interpolated / max_health) };
					else
						colors = { cfg.visuals.esp.health_color_1.get(),
								  color::interpolate(cfg.visuals.esp.health_color_1.get(), cfg.visuals.esp.health_color_2.get(), player.health_interpolated / max_health) };

					if (health > 0 && max_health > 0)
						add_bar(
							player, esp_item_pos::left, colors, player.health_interpolated / max_health,
							static_cast<float>(health) / max_health <= 0.93 ? std::optional(health) : std::nullopt);
				}

				if (cfg.visuals.esp.ping.get())
				{
					const auto ping = player_controller->get_ping();
					if (ping >= 100)
						add_bar(player, esp_item_pos::top, { cfg.visuals.esp.ping_color.get() }, static_cast<float>(ping) / 200, ping);
				}

				layer->font = draw.fonts[FNV1A("esp_name")];
				if (cfg.visuals.esp.name.get())
				{
					add_text(player, esp_item_pos::top, cfg.visuals.esp.name_color.get(), player_controller->get_s_sanitized_player_name().get(), true);
				}

				if (cfg.visuals.esp.flags->test(cfg_t::esp_flag_immune) && player_pawn->get_m_fImmuneToGunGameDamageTime().get_value() - sdk::GlobalVars->cur_time > 0.f)
				{
					add_text(
						player, esp_item_pos::right, color(
							color(1.f, 0.552f, 0.21f), std::min(player_pawn->get_m_fImmuneToGunGameDamageTime().get_value() - sdk::GlobalVars->cur_time, 1.f)),
						XOR("IMMUNE"));
				}

				if (cfg.visuals.esp.flags->test(cfg_t::esp_flag_money) && player_controller->get_in_game_money_services_ptr())
				{
					add_text(player, esp_item_pos::right, color{ 130, 180, 0 }, XOR("$") + std::to_string(player_controller->get_in_game_money_services_ptr()->get_account()));
				}
				if (cfg.visuals.esp.flags->test(cfg_t::esp_flag_armor) && (player_controller->get_pawn_has_helmet() || player_pawn->get_prev_armor_val() > 0))
				{
					const auto string = player_pawn->get_prev_helmet() ? XOR("HK") : XOR("K");
					add_text(player, esp_item_pos::right, color::white(), string);
				}
				/*if (player.can_hit && cfg.visuals.esp.flags->test(cfg_t::esp_flag_hit))
				{
					_(H, "HIT");
					const auto color = player.dormant ? ::color::gray(.5f, player.alpha / 255.f) : ::color(255.f, 255.f, 255.f, player.alpha, true);
					layer->add_text(vec2(top.x + width + 3, top.y + 8 * elements++ - 1), H, color, text_params::with_h(align_left));
				}*/
				if (cfg.visuals.esp.flags->test(cfg_t::esp_flag_scoped) && player_pawn->get_m_bIsScoped())
				{
					add_text(player, esp_item_pos::right, color(0.18f, 0.451f, 0.788f), XOR("SCOPED"));
				}

				// 武器名：weapon_id 查表（原 get_weapon_data + localize 链路已弃用）
				if (cfg.visuals.esp.weapon.get())
				{
					uint32_t wid = 0;
					if (read_weapon_id_seh(player_pawn, &wid))
						if (const auto name = lookup_weapon_name(wid))
							add_text(player, esp_item_pos::bottom, cfg.visuals.esp.weapon_color.get(), name);
				}
				// 武器图标：weapon_id 查表 → 本地 assets SVG 纹理（原 wpn_data->get_name 链路已弃用）
				if (cfg.visuals.esp.weapon_icon.get())
				{
					uint32_t wid = 0;
					if (read_weapon_id_seh(player_pawn, &wid))
					{
						if (const auto icon = lookup_weapon_icon(wid))
							add_icon(
								player, esp_item_pos::bottom, cfg.visuals.esp.weapon_color.get(),
								draw_mgr.get_panorama_texture(std::string(XOR("icons/equipment/")) + icon, 12));
						// [dbg 已静音] 图标未命中表的「wid not in table」一次性日志已删（未列出的 id 静默跳过）
					}
				}

				///*if (player.lc && !player.dormant && cfg.visuals.esp.flags->test(cfg_t::esp_flag_lc))
				//{
				//	_(lc_s, "LC");
				//	const auto color = player.dormant ? ::color::gray(.5f, player.alpha / 255.f) : ::color(130.f, 180.f, 0.f, player.alpha, true);
				//	layer->add_text(vec2(top.x + width + 3, top.y + 8 * elements++ - 1), lc_s, color, text_params::with_h(align_left));
				//}*/

				//auto has_zeus = false;
				//auto has_bomb = false;
				//if ((cfg.visuals.esp.flags->test(cfg_t::esp_flag_taser) || cfg.visuals.esp.flags->test(cfg_t::esp_flag_bomb)) && player_pawn->get_weapon_services_ptr() &&
				//	player_pawn->get_weapon_services_ptr()->get_h_my_weapons().size > 0)
				//{
				//	for (auto idx = 0; player_pawn->get_weapon_services_ptr()->get_h_my_weapons().data[idx].valid() && idx < player_pawn->get_weapon_services_ptr()->
				//		get_h_my_weapons().size; idx++)
				//	{
				//		const auto wpn = player_pawn->get_weapon_services_ptr()->get_h_my_weapons().data[idx].get();
				//		if (!wpn)
				//			continue;

				//		if (wpn->get_attribute_manager().get_item().get_item_definition_index() == sdk::weapon_id::weapon_taser)
				//			has_zeus = true;

				//		else if (wpn->get_attribute_manager().get_item().get_item_definition_index() == sdk::weapon_id::weapon_c4)
				//			has_bomb = true;
				//	}
				//}

				//if (cfg.visuals.esp.flags->test(cfg_t::esp_flag_taser) && has_zeus && wpn_item)
				//{
				//	const auto amount = fmod(sdk::GlobalVars->real_time, 0.5f) * 2.f;
				//	const auto opacity = static_cast<int>(ceilf(sdk::GlobalVars->real_time)) % 1 ? 1.f - amount : amount;
				//	const auto zeus_active = wpn_item->get_item_definition_index() == sdk::weapon_id::weapon_taser;
				//	add_icon(
				//		player, esp_item_pos::left, color(1.f, 0.902f, 0.068f, zeus_active ? opacity : 1.f), draw_mgr.get_svg_texture(FNV1A("taser"), 16), vec2{ 2, 1 }, 11);
				//}

				//if (cfg.visuals.esp.flags->test(cfg_t::esp_flag_defuser) && player_pawn->get_prev_defuser())
				//{
				//	add_icon(player, esp_item_pos::left, color(0.18f, 0.451f, 0.788f), draw_mgr.get_panorama_texture(XOR("icons/equipment/defuser"), 10), vec2{ -1, 0 });
				//}

				//if (cfg.visuals.esp.flags->test(cfg_t::esp_flag_bomb) && has_bomb)
				//{
				//	add_icon(player, esp_item_pos::left, color(0.788f, 0.188f, 0.247f), draw_mgr.get_panorama_texture(XOR("icons/equipment/c4"), 10), vec2{ -2, 0 });
				//}

			};

		if (player.alpha > 0.f)
		{
			if (player.oof)
			{
				player_out_of_fov(player);
				continue;
			}

			if (!cfg.visuals.esp.enabled.get())
				continue;

			draw_mgr.buf->g.alpha = player.alpha / 255.f;

			player_skeleton(player_pawn, player, layer);
			player_box();
			player_info();
		}
	}

	draw_mgr.buf->g.alpha = 1.f;
}

void visuals_t::draw_debug_info() const
{
	auto elems = 0;
	for (auto& elem : values)
	{
		draw_mgr.buf->font = draw.fonts[GUI_HASH("gui_main")];
		draw_mgr.buf->add_text(
			vec2{ 50, 500 + ++elems * 12.f }, tfm::format("[ %s ]\t %s", elem.first, elem.second.value), color::white(), text_params::with_vh(align_top, align_left));
	}
}

void visuals_t::add_text(player_data_t& player, const esp_item_pos pos, const color& color, const std::string& text, bool is_name)
{
	auto& offset = player.get_offset(pos);

	constexpr auto font_height = 9;
	draw_mgr.buf->font = is_name ? draw.fonts[FNV1A("esp_name")] : draw.fonts[FNV1A("esp")];

	switch (pos)
	{
	case esp_item_pos::top:
		draw_mgr.buf->add_text(
			vec2{ player.top.x + static_cast<float>(is_name ? 0 : 2), player.top.y - offset }, text, color, text_params::with_vh(align_bottom, align_center));
		offset += font_height;
		break;
	case esp_item_pos::bottom:
		draw_mgr.buf->add_text(
			vec2{ player.bot.x + static_cast<float>(is_name ? 0 : 2), player.bot.y + offset }, text, color, text_params::with_vh(align_top, align_center));
		offset += font_height;
		break;
	case esp_item_pos::right:
		draw_mgr.buf->add_text(
			vec2{ player.top.x + player.width + offset + 3, player.top.y + player.offset.text_right }, text, color, text_params::with_vh(align_top, align_left));
		player.offset.text_right += font_height;
		break;
	case esp_item_pos::left:
		draw_mgr.buf->add_text(
			vec2{ player.top.x - player.width - offset, player.top.y + player.offset.text_left }, text, color, text_params::with_vh(align_top, align_right));
		player.offset.text_left += font_height;
		break;
	}
}


void visuals_t::add_icon(
	player_data_t& player, const esp_item_pos pos, const color& color, const std::shared_ptr<texture>& texture, std::optional<vec2> add,
	std::optional<int> height_override)
{
	if (!texture)
		return;

	auto& offset = player.get_offset(pos);

	const auto int_height = height_override.value_or(static_cast<int>(ceilf(texture->get_size().y)));
	switch (pos)
	{
	case esp_item_pos::top:
		draw_mgr.add_icon(vec2{ player.top.x, player.top.y - offset } + add.value_or(vec2{}), color, texture, text_params::with_vh(align_bottom, align_center));
		offset += int_height + 1;
		break;
	case esp_item_pos::bottom:
		draw_mgr.add_icon(vec2{ player.bot.x, player.bot.y + offset } + add.value_or(vec2{}), color, texture, text_params::with_vh(align_top, align_center));
		offset += int_height + 1;
		break;
	case esp_item_pos::right:
		draw_mgr.add_icon(
			vec2{ player.top.x + player.width + offset + 3, player.top.y + player.offset.text_right } + add.value_or(vec2{}), color, texture,
			text_params::with_vh(align_top, align_left));
		player.offset.text_right += int_height + 1;
		break;
	case esp_item_pos::left:
		draw_mgr.add_icon(
			vec2{ player.top.x - player.width - offset, player.top.y + player.offset.text_left } + add.value_or(vec2{}), color, texture,
			text_params::with_vh(align_top, align_right));
		player.offset.text_left += int_height + 1;
		break;
	}
}

void visuals_t::add_bar(player_data_t& player, const esp_item_pos pos, const std::vector<color>& colors, float fraction, std::optional<int> value)
{
	if (colors.empty())
		return;

	auto& offset = player.get_offset(pos);
	switch (pos)
	{
	case esp_item_pos::top:
	case esp_item_pos::bottom:
	{
		const auto is_top = pos == esp_item_pos::top;
		const auto start_pos = vec2{ player.top.x - player.width, player.bot.y + (is_top ? -5 - offset : 1 + offset) };
		draw_bar(start_pos, player.width * 2, true, colors, fraction, value);
		break;
	}
	case esp_item_pos::right:
	case esp_item_pos::left:
	{
		const auto is_left = pos == esp_item_pos::left;
		const auto start_pos = vec2{ player.bot.x - player.width + (is_left ? -5 - offset : 1 + offset), player.bot.y };
		draw_bar(start_pos, player.height, false, colors, fraction, value);
		break;
	}
	}

	offset += 5;
}

void visuals_t::draw_bar(const vec2& start_pos, int length, bool horizontal, const std::vector<color>& colors, float fraction, std::optional<int> value)
{
	const auto multiplier = clamp(fraction, 0.f, 1.f);
	const auto multicolor = colors.size() > 1;
	const auto bar_length = floor((length - 2) * multiplier);

	const auto size = vec2{ horizontal ? length : 4.f, horizontal ? 4.f : -length };
	const auto bar_size = vec2{ horizontal ? bar_length : 2.f, horizontal ? 2.f : -bar_length };
	const auto bar_pos = horizontal ? start_pos + 1 : start_pos + vec2{ 1, -1 };

	draw_mgr.buf->add_rect_filled(rect(start_pos).size(size), ::color(color::black(), 0.4f));
	if (multicolor)
		draw_mgr.buf->add_rect_filled_multicolor(rect(bar_pos).size(bar_size), { colors[horizontal ? 1 : 0], colors[0], colors[horizontal ? 0 : 1], colors[1] });
	else
		draw_mgr.buf->add_rect_filled(rect(bar_pos).size(bar_size), colors[0]);

	draw_mgr.buf->font = draw.fonts[FNV1A("esp")];
	if (value.has_value())
	{
		const auto text_pos = vec2{ horizontal ? start_pos.x + 2 + bar_length : start_pos.x + 4, horizontal ? start_pos.y + 4 : start_pos.y - bar_length - 2 };
		draw_mgr.buf->add_text(text_pos, std::to_string(value.value()), ::color::white(), text_params::with_vh(align_center, align_center));
	}
}

void visuals_t::player_skeleton(sdk::cs2_player_pawn* pawn, player_data_t& player, const std::shared_ptr<evo::ren::layer>& layer)
{
	if (!cfg.visuals.esp.skeleton.get() || !pawn)
		return;

	constexpr uint32_t bone_count = 30;
	sdk::vector joints[bone_count];
	if (!read_bones_seh(pawn, joints, bone_count))
		return;

	// 合理性校验：人形模型关节（骨盆 index 1）应贴近玩家原点，错误偏移/步长会给出垃圾坐标
	if (joints[1].dist(player.pos) > 100.f)
		return;

	// 关节索引链来自外部实测索引表（2026-04 animgraph 更新后 root_motion 占 index 0）
	constexpr std::pair<uint8_t, uint8_t> chains[] = {
		{1, 2}, {2, 4}, {4, 6}, // pelvis→spine_1→spine_2→neck
		{6, 7}, // neck→head
		{6, 9}, {9, 10}, {10, 11}, // 左臂
		{6, 13}, {13, 14}, {14, 15}, // 右臂
		{1, 17}, {17, 18}, {18, 19}, // 左腿
		{1, 20}, {20, 21}, {21, 22}, // 右腿
	};

	const auto col = cfg.visuals.esp.skeleton_color.get();

	vec2 screen[bone_count]{};
	bool projected[bone_count]{};

	for (const auto& link : chains)
	{
		const auto a = link.first;
		const auto b = link.second;

		if (!projected[a])
		{
			vec2 ndc{};
			if (math::world_to_screen(joints[a], ndc))
			{
				screen[a] = ndc_to_px(ndc);
				projected[a] = true;
			}
		}

		if (!projected[b])
		{
			vec2 ndc{};
			if (math::world_to_screen(joints[b], ndc))
			{
				screen[b] = ndc_to_px(ndc);
				projected[b] = true;
			}
		}

		if (projected[a] && projected[b])
			layer->add_line(screen[a], screen[b], col);
	}
	// [dbg 已静音] 一次性面包屑「skeleton drawn」——骨骼绘制链已稳定
}

void visuals_t::player_out_of_fov(player_data_t& player)
{
	if (!cfg.visuals.out_of_fov.get())
		return;

	// [dbg 已静音] 一次性面包屑「oof path entered」——oof 链已稳定

	sdk::vector eye{};
	float yaw = 0.f;
	if (!read_local_eye_seh(&eye, &yaw))
		return;

	const auto delta = player.pos - eye;
	if (delta.length_2d() < 1.f)
		return;

	// 相对视线 yaw 的方位角：0=屏幕正上，正值=左侧（Source 正 yaw 向左转）
	const auto rel_yaw = math::normalize_yaw(RAD2DEG(std::atan2(delta.y, delta.x)) - yaw);
	const auto rad = DEG2RAD(rel_yaw);

	const auto size = draw.display;
	const auto center = vec2{ size.x * 0.5f, size.y * 0.5f };

	// 椭圆边缘映射：横轴贴左右边、纵轴贴顶底边（圆周统一半径会让上下箭头压在屏幕边缘不可见）。
	// out_of_fov_dst 为边缘内缩像素
	const auto mx = size.x * 0.5f - clamp(cfg.visuals.out_of_fov_dst.get(), 0.f, size.x * 0.5f);
	const auto my = size.y * 0.5f - clamp(cfg.visuals.out_of_fov_dst.get(), 0.f, size.y * 0.5f);

	// 该分支在 g.alpha 被上一轮玩家改写之后执行，箭头保持全透明度
	draw_mgr.buf->g.alpha = 1.f;

	const auto dir = vec2{ -std::sin(rad), -std::cos(rad) }; // 单位圆方向：0=屏幕正上，+90°=屏幕正左
	const auto side = vec2{ -dir.y, dir.x };
	const auto pos = center + vec2{ dir.x * mx, dir.y * my }; // 椭圆边缘位置

	const auto arrow = cfg.visuals.out_of_fov_size.get();
	const auto tip = pos + dir * arrow;
	const auto a = pos - dir * (arrow * 0.6f) + side * (arrow * 0.5f);
	const auto b = pos - dir * (arrow * 0.6f) - side * (arrow * 0.5f);

	draw_mgr.buf->add_triangle_filled(tip, a, b, cfg.visuals.out_of_fov_col.get());
}


/*







�� ��������� �.�.
� 1 ���� 2023 �� 1 ������� 2023
�������� ������� �� ��������� ����������� ����










(�� ��������� ��������) �������� ������ ����� 30%


*/