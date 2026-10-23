#pragma once

#include <atomic>
#include <D3D11.h>
#include <sdk/client.h>
#include "../minhook/minhook.h"
#include "../src/memory/detour_hook.h"

namespace hooks
{
	namespace input_system
	{
		LRESULT wnd_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);
		bool mouse_input_enabled(void *rcx);
		inline CBaseHookObject<decltype(&mouse_input_enabled)> hkMouseInputEnabled = {};
	}

	namespace steam
	{
		HRESULT present(IDXGISwapChain *chain, UINT sync, UINT flags);
		inline CBaseHookObject<decltype(&present)> hkPresent = {};
	}

	namespace client
	{
		void prediction_update(void* pred, int a, int b);
		void* create_move(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7);
		inline CBaseHookObject<decltype(&create_move)> hkCreateMove = {};
		__int64 myTraceLineIntervals(__int64* a1, int* out, __int64 a3, __int64 a4, BYTE* a5, int a6);
		inline CBaseHookObject<decltype(&myTraceLineIntervals)> hkTraceLineIntervals = {};
		// [silent aim] sub_CFA140（create_move 内部"填命令视角"，用户 IDA+FPU 断点+
		// NOP 实验实锤：命令 viewangles 唯一写入点）。rcx=输入视角快照（+0x10 pitch/
		// +0x14 yaw/+0x18 roll），rdx=命令上下文，[rdx+0x18]=CUserCmd（+0x10 dirty
		// 位图、+0x18/+0x1C/+0x20=viewangles P/Y/R）。签名多参（IDA arg_0/8/10/20/28），
		// 8 槽透传；original 填完后改写为 aimbot 目标角 → 服务端打头、本地视角不动。
		void* fill_cmd_angles(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7);
		inline CBaseHookObject<decltype(&fill_cmd_angles)> hkFillCmdAngles = {};
		void frame_stage_notify(void *rcx, sdk::client_frame_stage stage);
		void override_view(void *rcx, sdk::cview_setup *view_setup);
		// [thirdperson] ClientMode::OverrideView 钩（RVA 0xD02310）：convar 驱动肩部摄像机
		inline CBaseHookObject<decltype(&override_view)> hkOverrideView = {};
		void on_render_start(sdk::cview_render *view_render);
		float get_fov(void *rcx);
		inline CBaseHookObject<decltype(&get_fov)> hkGetFov = {};
		inline CBaseHookObject<decltype(&on_render_start)> hkOnRenderStart = {};

		// [aw-verify] F5 在 present 中置位，trace_funnel 下一次调用时单次 dump 仅日志
		inline std::atomic<bool> trace_dump_armed{ false };

		void* bullet_trace_helper(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7);
		inline CBaseHookObject<decltype(&bullet_trace_helper)> hkBulletTrace = {};

		// [14190] ForceButtonsDown（sub_A66050）钩 —— autofire 注入点：
		// a1 = 64 位按钮位集（值传递，全量覆盖语义），武装帧 OR IN_ATTACK(bit0)。
		// 8 槽透传（该函数有栈参）。
		void force_buttons_down(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7);
		inline CBaseHookObject<decltype(&force_buttons_down)> hkForceButtonsDown = {};
		// create_move 钩子调用：解除帧重放原始掩码清注入位（物理按键位保留）
		void autofire_release_check();
		// create_move 钩子调用：武装帧主动调 ForceButtonsDown（游戏只在清位时调它，
		// 被动钩等不到窗口）—— a1 = dev+0x190 当前按下态 | IN_ATTACK
		void autofire_inject_check();

		// [aw-verify] 漏斗/展开器/子弹helper调用总计数（present 的超时警告读取，判断冷/热）
		inline std::atomic<unsigned> trace_funnel_calls{ 0 };
		inline std::atomic<unsigned> trace_expander_calls{ 0 };
		inline std::atomic<unsigned> bullet_helper_calls{ 0 };

		// [aimbot 可见性] 眼位→目标点同步 trace（自调用漏斗，p5=1）。
		// 返回 fraction，出参 hit_ent = 命中实体指针（+0x08，miss 为 null）；故障返回 1/null。
		float aw_trace_query(const float start[3], const float end[3], const void** hit_ent);

		// [aimbot 可见性] 眼位→目标点同步 trace（自调用漏斗，p5=1）；
		// false=被阻挡（命中非目标实体）。trace 故障按可见处理（保守）。
		// 命中目标 pawn 自身也算可见（骨骼点在 hitbox 内部，trace 停在目标表面）。
		bool aw_visible_check(const float start[3], const float end[3], const void* target_pawn);

		// [aw-verify] 11 个 trace filter 的 dtor 钩（vtable slot0）。栈上 filter 在
		// trace 完成后析构 → 反查开火 trace 函数 + 同栈扫已完成 CGameTrace。
		// 顺序见 offsets::functions::filter_dtors 与 client.cpp 的 aw_filter_meta。
		constexpr int AW_FILTER_COUNT = 11;
		inline std::atomic<unsigned> filter_dtor_calls[AW_FILTER_COUNT]{};

		void* aw_filter_dtor00(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor00)> hkFilterDtor00 = {};
		void* aw_filter_dtor01(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor01)> hkFilterDtor01 = {};
		void* aw_filter_dtor02(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor02)> hkFilterDtor02 = {};
		void* aw_filter_dtor03(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor03)> hkFilterDtor03 = {};
		void* aw_filter_dtor04(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor04)> hkFilterDtor04 = {};
		void* aw_filter_dtor05(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor05)> hkFilterDtor05 = {};
		void* aw_filter_dtor06(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor06)> hkFilterDtor06 = {};
		void* aw_filter_dtor07(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor07)> hkFilterDtor07 = {};
		void* aw_filter_dtor08(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor08)> hkFilterDtor08 = {};
		void* aw_filter_dtor09(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor09)> hkFilterDtor09 = {};
		void* aw_filter_dtor10(void* self, unsigned char free_flags);
		inline CBaseHookObject<decltype(&aw_filter_dtor10)> hkFilterDtor10 = {};

		// [aw-verify] client 同步 trace 漏斗（RVA 0xA1A890，5 个查询入口共用）。
		// 全部参数都是指针/句柄类（Ray_t*、filter*、mask 等），按 8 个通用寄存器/栈槽
		// 透传即可；多声明的栈参数写在自己的 outgoing area，对 caller cleanup 无影响。
		// 必须原样返回 original 的 rax。
		void* trace_funnel(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7);
		inline CBaseHookObject<decltype(&trace_funnel)> hkTraceFunnel = {};

		// [aw-verify] trace 展开器（RVA 0x9D31A0，「0x48 记录→CGameTrace」展开）。
		// 8 槽透传（漏斗教训：出参可能在第 7/8 参数栈传；多声明的栈参数写在
		// 自己的 outgoing area，对 caller cleanup 无影响）。
		void* trace_expander(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7);
		inline CBaseHookObject<decltype(&trace_expander)> hkTraceExpander = {};
	}
} // namespace hooks
