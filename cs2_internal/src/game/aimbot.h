#pragma once
#include <sdk/client.h>

struct aimbot_t
{
	static void run(sdk::ccsgo_input* input);

	// [silent] 本 tick 目标角：run 在 create_move original 之前设置，fill_cmd_angles
	// 钩（original 内部）消费——同输入线程单生产单消费，无需同步
	static bool silent_valid;
	static float silent_pitch;
	static float silent_yaw;
	// [14190] autofire 注入标志：锁定+可见+复选框开 = true；
	// ForceButtonsDown 钩（client.cpp）消费——武装帧 OR IN_ATTACK 位

	static bool wants_autofire;
	// [visualize] 锁定目标的瞄准点快照（本帧）：visuals 层每帧画标记，
	// vis_safe=该点 trace 可见（隔墙/失效时用 aim 色区分）
	static bool vis_valid;
	static bool vis_safe;
	static sdk::vector vis_pos;
	// [autostop] 锁定目标时请求停止移动（create_move post-original 消费，
	// 清当前命令 forwardmove/sidemove）

	static bool wants_autostop;
};
