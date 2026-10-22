#pragma once
#include <sdk/client.h>

struct aimbot_t
{
	static void run(sdk::ccsgo_input* input);
	void scale_damage(const int hitgroup, const sdk::cs2_base_entity* entity);
	// [silent] 本 tick 目标角：run 在 create_move original 之前设置，fill_cmd_angles
	// 钩（original 内部）消费——同输入线程单生产单消费，无需同步
	static bool silent_valid;
	static float silent_pitch;
	static float silent_yaw;
	// [14190] autofire 注入标志：锁定+可见+复选框开 = true；
	// ForceButtonsDown 钩（client.cpp）消费——武装帧 OR IN_ATTACK 位

	static bool wants_autofire;
	
	
	// vis_safe=该点 trace 可见（隔墙/失效时用 aim 色区分）
	static bool vis_valid;
	static bool vis_safe;
	static sdk::vector vis_pos;

	static bool wants_autostop;
	
};
// 骨骼索引表（2026-04 animgraph 后）：
	// pelvis=1/spine_1=2/spine_2=4/neck=6/head=7/臂9-15/腿17-22（root_motion 占 0）
	// —— 与 visuals 骨架同源
inline constexpr uint32_t k_bone_pelvis = 1;
inline constexpr uint32_t k_bone_spine1 = 2;
inline constexpr uint32_t k_bone_spine2 = 4;
inline constexpr uint32_t k_bone_neck = 6;
inline constexpr uint32_t k_bone_head = 7;
inline constexpr uint32_t k_bone_arm_first = 9;
inline constexpr uint32_t k_bone_arm_last = 15;
inline constexpr uint32_t k_bone_leg_first = 17;
inline constexpr uint32_t k_bone_leg_last = 20;
inline constexpr uint32_t k_bone_foot_l = 21;
inline constexpr uint32_t k_bone_foot_r = 22;
inline constexpr uint32_t k_bone_max = 22;
inline UINT delay_tick = 0;//211tick=1s