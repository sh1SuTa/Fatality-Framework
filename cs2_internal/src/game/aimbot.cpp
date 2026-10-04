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

namespace
{
	// [aim-diag 已删] 64 tick 滚动统计（calls/nocmd/noeye/enemies/found/fired）——
	// 管线已验证稳定（locks=128/128、autofire 实测成功），死计数器不再保留

	// 骨骼索引表（2026-04 animgraph 后）：head=7 —— 与 visuals 骨架同源
	constexpr uint32_t k_bone_head = 7;
	constexpr float k_rad2deg = 180.f / 3.14159265f;

	// 骨骼关节布局同 visuals.cpp（0x20 步长），数组指针在 GameSceneNode+0x1C0
	struct bone_joint_t
	{
		sdk::vector pos;
		float scale;
		char pad[0x10];
	};
	static_assert(sizeof(bone_joint_t) == 0x20, "bone joint stride mismatch");

	// 整条骨骼读取链 SEH 兜底（pawn 可能悬垂、偏移可能过期）
	__declspec(noinline) bool read_head_seh(sdk::cs2_player_pawn* pawn, sdk::vector* out)
	{
		__try
		{
			const auto node = pawn->get_m_pGameSceneNode();
			if (!node)
				return false;
			const auto joints = *reinterpret_cast<bone_joint_t**>(reinterpret_cast<uint8_t*>(node) + 0x1C0);
			if (!joints)
				return false;
			*out = joints[k_bone_head].pos;
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

	// [autofire] SendInput 合成左键（按钮位静态扫描三连败后的务实方案，零逆向依赖）：
	// 锁头已把准星物理吸到目标头上，合成点击后游戏自身完成 IN_ATTACK→命令→开火全管线。
	int g_click_phase = 0; // 0=松开 1..4=按下剩余 tick（~12.8 CPS 脉冲）

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
		g_click_phase = 0;
	}
}

void aimbot_t::run(sdk::ccsgo_input* input)
{
	if (!cfg.aim.enabled.get() || !input)
		return;

	// 菜单打开时冻结（不锁人不合成点击），并松掉可能挂着的合成左键
	if (menu::men.is_open())
	{
		if (g_click_phase)
			syn_click_up();
		return;
	}

	float cur_p = 0.f, cur_y = 0.f, cur_r = 0.f;
	if (!aim_read_view(input, &cur_p, &cur_y, &cur_r))
		return;

	sdk::vector eye{};
	if (!read_local_eye_seh(&eye))
		return;

	const auto max_fov = cfg.aim.fov.get();

	// FOV 最小目标优选（遍历方式与 visuals ESP 主循环一致）
	sdk::vector best_head{};
	auto best_fov = max_fov;
	auto found = false;

	for (auto i = 1; i <= sdk::max_players; ++i)
	{
		const auto ctrl = sdk::GameEntitySystem->get_player_controller(i);
		if (!ctrl || ctrl->get_is_local_player_controller())
			continue;
		const auto pawn = sdk::GameEntitySystem->get_player_pawn(i);
		if (!pawn || !pawn->is_alive() || !pawn->is_enemy())
			continue;

		sdk::vector head{};
		if (!read_head_seh(pawn, &head))
			continue;

		float pitch = 0.f, yaw = 0.f;
		calc_angle(eye, head, &pitch, &yaw);
		const auto dp = angle_diff(pitch, cur_p);
		const auto dy = angle_diff(yaw, cur_y);
		const auto fov = std::sqrt(dp * dp + dy * dy);
		if (fov < best_fov)
		{
			best_fov = fov;
			best_head = head;
			found = true;
		}
	}

	if (found)
	{
		// 可见性检查（自调用同步 trace，见 hooks::client::aw_visible_check）：
		// 眼位→目标头部被世界几何阻挡则不锁不开火；trace 故障按可见处理（保守）。
		// 不可见时松掉可能挂着的合成左键（防隔墙余弹）。
		const float st[3] = {eye.x, eye.y, eye.z};
		const float ed[3] = {best_head.x, best_head.y, best_head.z};
		if (!hooks::client::aw_visible_check(st, ed))
		{
			if (g_click_phase > 0)
				syn_click_up();
			return;
		}

		float pitch = 0.f, yaw = 0.f;
		calc_angle(eye, best_head, &pitch, &yaw);

		// v1 直写 live 视角槽：非 silent，准星每 tick snap 到目标头（先验证管线）
		aim_write_view(input, pitch, yaw);
	}

	// [autofire] 目标锁定即脉冲点击（~12.8 CPS）；用户物理按住左键时不抢按
	// （避免合成 UP 打断泼水），目标丢失/关开关/开菜单立即松键
	const bool want_fire = found && cfg.aim.autofire.get();
	if (want_fire && g_click_phase == 0 && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
	{
		syn_click_down();
		g_click_phase = 1;
	}
	else if (g_click_phase > 0 && (!want_fire || ++g_click_phase > 4))
	{
		syn_click_up();
	}
}
