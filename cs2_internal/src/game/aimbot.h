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
};
