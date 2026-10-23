#include <cstring>
#include <cmath>
#include <cstdio>
#include <intrin.h>
#include <macros.h>
#include <game/aimbot.h>
#include <game/cfg.h>
#include <game/draw_manager.h>
#include <game/game.h>
#include <game/hook_manager.h>
#include <game/movement.h>
#include <game/visuals.h>
#include <gui/controls/selectable_script.h>
#include <menu/menu.h>
#include <menu/init/init_anim.h>
#include <sdk/client.h>
#include <sdk/cvar.h>
#include <sdk/engine.h>
#include <sdk/input.h>
#include <sdk/offsets.h>
#include <sdk/sdk.h>
#include <sdk/trace.h>
#include <utils/mem.h>
#include"../sdk/utl/utl_vector.h"
#include"../sdk/trace.h"
namespace
{
	// [aw-verify] 可读性检查（VirtualQuery 版，与 hook_manager.cpp 同款）
	bool aw_is_readable(const void* p)
	{
		if (p == nullptr)
			return false;
		MEMORY_BASIC_INFORMATION info{};
		if (!VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
			return false;
		const auto protection = info.Protect & 0xff;
		return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
			protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
	}

	// [aw-verify] SEH 扁平读取 helper —— 函数体内不得出现 C++ 对象（C2712）
	__declspec(noinline) bool aw_read_qword(const void* p, unsigned long long* out)
	{
		__try
		{
			*out = *static_cast<const unsigned long long*>(p);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	__declspec(noinline) bool aw_read_dword(const void* p, unsigned long* out)
	{
		__try
		{
			*out = *static_cast<const unsigned long*>(p);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	__declspec(noinline) bool aw_read_float(const void* p, float* out)
	{
		__try
		{
			*out = *static_cast<const float*>(p);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// [aw-verify] slot56 surface getter 调用（展开器证据：f(rcx=obj, rdx=栈上出参) → rax 指向 32B surface）。
	// 独立 noinline 函数：函数体内不得出现 C++ 对象（C2712 惯例）
	__declspec(noinline) bool aw_call_slot56(void* fn, void* self, unsigned char* buf32, unsigned char** data_out)
	{
		__try
		{
			const auto f = reinterpret_cast<void* (__fastcall*)(void*, void*)>(fn);
			const auto r = f(self, buf32);
			*data_out = reinterpret_cast<unsigned char*>(r);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// [aw-verify] vtable 指针 → 已知 filter 名（RTTI 核验 RVA 表，无命中返回 null）
	const char* aw_filter_name(const unsigned long long vtable, const uintptr_t client_base)
	{
		if (vtable <= client_base)
			return nullptr;
		const auto rva = vtable - client_base;
		namespace tv = sdk::offsets::functions::trace_vtables;
		switch (rva)
		{
			case tv::ctrace_filter: return "CTraceFilter";
			case tv::ccs_trace_filter_simple: return "CCSTraceFilterSimple";
			case tv::no_npcs_or_player: return "NoNPCsOrPlayer";
			case tv::player_movement_cs: return "PlayerMovementCS";
			case tv::entity_sweep: return "EntitySweep";
			case tv::omit_players: return "OmitPlayers";
			case tv::no_combat_characters: return "NoCombatCharacters";
			default: return nullptr;
		}
	}

	// [aw 已收口] aw_log 辅助函数已删（所有调用点已静音，无活引用）

	// [aw-verify] 结构性 CGameTrace 判定（SEH 守护，全程只读）：
	// fraction@+0xAC∈[0,1.01] 且 endpos@+0x78 finite 且非全零 且 handle(+0x68)!=0xffffffff
	// 且 hit(+0xBB)/flag_BA(+0xBA)∈{0,1}
	// （全零 endpos / handle=0xffffffff = 垃圾结构特征：实测普通结构体的浮点垃圾
	// 恰好落进 fraction 区间即可通过旧判定，白吃 armed）
	bool aw_trace_shape(const void* q, float* fraction_out)
	{
		const auto p = static_cast<const unsigned char*>(q);
		if (!aw_is_readable(p))
			return false;

		float f = 0.f, e0 = 0.f, e1 = 0.f, e2 = 0.f;
		if (!aw_read_float(p + 0xAC, &f) || !(f >= 0.0f && f <= 1.01f))
			return false;
		if (!aw_read_float(p + 0x78, &e0) || !aw_read_float(p + 0x7C, &e1) || !aw_read_float(p + 0x80, &e2))
			return false;
		if (!std::isfinite(e0) || !std::isfinite(e1) || !std::isfinite(e2))
			return false;
		if (e0 == 0.0f && e1 == 0.0f && e2 == 0.0f)
			return false;

		unsigned long fd = 0;
		if (!aw_read_dword(p + 0xB8, &fd))
			return false;
		if (((fd >> 24) & 0xFF) > 1 || ((fd >> 16) & 0xFF) > 1)
			return false;

		unsigned long handle = 0;
		if (!aw_read_dword(p + 0x68, &handle) || handle == 0xFFFFFFFFul)
			return false;

		*fraction_out = f;
		return true;
	}

	// [aw-verify] 命中型 trace 判定（funnel/expander 共用）：
	// hit==1，或 fraction<1 且法线非竖直（|normal.z|<0.5）。后者用于排除地面/移动探针
	// —— 这类 trace 恒定存在且 normal.z≈+1，F5 后一帧内就会吃掉 armed；打竖直墙的
	// 子弹 trace 法线接近水平。打墙请瞄竖直平面，打地板不会触发 dump。
	bool aw_pick_hit_trace(const void** args, int count, int* pick_out, float* fraction_out)
	{
		for (int i = 0; i < count; ++i)
		{
			float f = 0.f;
			if (!aw_trace_shape(args[i], &f))
				continue;

			unsigned long fd = 0;
			aw_read_dword(static_cast<const unsigned char*>(args[i]) + 0xB8, &fd);
			const auto hit = (fd >> 24) & 0xFF; // +0xBB

			if (hit == 1)
			{
				*pick_out = i;
				*fraction_out = f;
				return true;
			}

			float nz = 0.f;
			if (f > 1e-6f && f < 1.0f
				&& aw_read_float(static_cast<const unsigned char*>(args[i]) + 0x98, &nz)
				&& nz > -0.5f && nz < 0.5f)
			{
				*pick_out = i;
				*fraction_out = f;
				return true;
			}
		}
		return false;
	}

	// [aw-verify] CGameTrace 布局字段 dump（funnel/expander 共用，p 已过 aw_trace_shape）
	void aw_dump_trace_fields(const unsigned char* p, int slot, float fraction)
	{
		unsigned long long hit_entity = 0;
		unsigned long handle = 0, legacy = 0, mats = 0;
		float endpos[3] = {}, normal[3] = {};
		float maxspeed = 0.f, jumpfactor = 0.f, distmod = 0.f, dmgmod = 0.f;
		unsigned long hit_byte = 0, flag_ba = 0, climbable = 0;

		const bool ok_entity = aw_read_qword(p + 0x08, &hit_entity);
		const bool ok_handle = aw_read_dword(p + 0x68, &handle);
		const bool ok_endpos = aw_read_float(p + 0x78, &endpos[0]) && aw_read_float(p + 0x7C, &endpos[1]) && aw_read_float(p + 0x80, &endpos[2]);
		const bool ok_normal = aw_read_float(p + 0x90, &normal[0]) && aw_read_float(p + 0x94, &normal[1]) && aw_read_float(p + 0x98, &normal[2]);
		const bool ok_surface = aw_read_float(p + 0x30, &maxspeed) && aw_read_float(p + 0x34, &jumpfactor) &&
			aw_read_float(p + 0x38, &distmod) && aw_read_float(p + 0x3C, &dmgmod);
		const bool ok_mats = aw_read_dword(p + 0x40, &legacy) && aw_read_dword(p + 0x44, &mats);
		const bool ok_flags = aw_read_dword(p + 0x48, &climbable);
		const bool ok_hit = aw_read_dword(p + 0xB8, &hit_byte) && aw_read_dword(p + 0xBA, &flag_ba);

		const auto hit = (hit_byte >> 24) & 0xFF; // +0xB8 dword 高字节 = +0xBB
	(void)hit;

	// [aw-verify 已收口] dump 字段日志已静音（函数定义保留，需要时取消注释）
	}

	// [aw-verify] filter dtor 事件（11 个 dtor 钩共用）：
	// ① 计数器；② 新调用者去重（注入起累计，背景探针调用者在 F5 前已记录，
	// 武装后只可能被「开火函数」这类新调用者触发）；③ armed 时记录 filter 名/self/
	// 调用者 RVA（离线用 .pdata 反查开火 trace 函数），并在同栈窗口向上 0x4000 内
	// 按指针槽扫描已完成的 CGameTrace（filter 析构时 trace 已填充完毕）；
	// 扫到 fraction<1 的命中 trace 即解武装。
	const char* const aw_filter_meta[11] = {
		"CTraceFilter", "CCSTraceFilterSimple", "NoNPCsOrPlayer", "PlayerMovementCS",
		"EntitySweep", "EntityPush", "OmitPlayers", "NoCombatCharacters",
		"KnifeIgnoreTeammates", "TaserIgnoreTeammates", "ForPlayerHeadCollision"
	};

	constexpr int AW_SEEN_MAX = 96;
	const void* aw_seen_callers[AW_SEEN_MAX] = {};
	int aw_seen_n = 0;
	int aw_dtor_logs = 0;

	// [btn2] create_move 全参数扫描探针 —— a4 被证伪（常 null/模块字符串/堆但表为0），
	// r15 宿主需从 a0-a7 实测定位。LMB 按下沿武装，held(n=4)/rel/idle 三态各一行：
	// 对每个可读 arg 读 +0x58(byte) 与 +0xBD0(表指针)，翻转者=按钮态、tbl=ok 者=宿主。
	int btn_win_n = -1;  // -1 = 未武装
	bool btn_prev_lmb = false;
	bool btn2_rel = false;
	int btn2_rel_n = 0;
	int btn2_refractory = 0;
	const unsigned char* btn2_gs = nullptr;   // 全局单例 qword_24C4FA0 指向的对象

	// SEH 扁平拷贝（命令对象可能悬垂，读崩就跳过）
	__declspec(noinline) bool btn_seh_copy(const void* p, int bytes, unsigned char* out)
	{
		__try
		{
			std::memcpy(out, p, static_cast<size_t>(bytes));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// [14190] ForceButtonsDown 注入状态（钩子与 create_move 解除逻辑共享）。
	// a1 = 64 位按钮位集（值传递，全量覆盖语义：+0x58 允许掩码按位与、
	// +0x190 状态 cmp 去重）—— sub_A66050 反汇编实锤。
	void* s_fbd_dev = nullptr;
	unsigned long long s_fbd_mask = 0;
	void* s_fbd_x[6] = {};   // original 其余槽位（解除时 8 槽重放）
	bool s_fbd_injected = false;

	struct aw_stack_trace_cand { const unsigned char* p; float fraction; int hit; };

	__declspec(noinline) void aw_filter_dtor_event(int id, const void* self, const void* retaddr)
	{
		hooks::client::filter_dtor_calls[id].fetch_add(1, std::memory_order_relaxed);

		
		static bool prev_armed = false;
		const bool now_armed = hooks::client::trace_dump_armed.load(std::memory_order_relaxed);
		if (now_armed && !prev_armed)
			aw_dtor_logs = 0;
		prev_armed = now_armed;

		for (int i = 0; i < aw_seen_n; ++i)
			if (aw_seen_callers[i] == retaddr)
				return;
		if (aw_seen_n < AW_SEEN_MAX)
			aw_seen_callers[aw_seen_n++] = retaddr;

		if (!hooks::client::trace_dump_armed.load(std::memory_order_relaxed) || aw_dtor_logs >= 12)
				return;
			++aw_dtor_logs;

			// [aw-verify 已收口] fdtor 事件 + 栈扫描日志全部静音（dtor 钩本身已撤装，
			// 此函数无人调用，仅保留代码备查）
		}
	}

namespace hooks::client
{
	// [anti-aim] 本 tick 实际写进 CUserCmd 的假角（以真实视角为基准算出的绝对值）。
	// fill_cmd_angles（输入线程）写，on_render_start（渲染线程）消费：本地第三人称模型的
	// m_angEyeAngles 必须显示"发给服务端的那份角"，否则本地看到的仍是真实朝向。
	// 32 位对齐读写，x86 下天然原子；最坏情况只差一帧，不加锁。
	float aa_model_pitch = 0.f;
	float aa_model_yaw = 0.f;
	bool aa_model_valid = false;

	// [anti-aim] 本地第三人称模型朝向写入（14192 client_dll dump：C_CSPlayerPawn
	// m_angEyeAngles = +0x3600，pitch/yaw 为前两个 float）。pawn 可能悬垂 → SEH 兜底。
	// [aa-mdl] 1s 一次的探针：before 是游戏上一帧留下的值（若恒等于 wrote 说明本写入
	// 没被覆盖；若不断变化说明 original 会重算，需把写入点继续前移）。
	__declspec(noinline) void aa_write_local_model(float pitch, float yaw)
	{
		__try
		{
			const auto ctrl = sdk::LocalPlayerController;
			if (!ctrl)
				return;

			auto* const pawn = ctrl->get_pawn();
			if (!pawn)
				return;

			auto* const ea = reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(pawn) + 0x3600);
			const float before_pitch = ea[0];
			const float before_yaw = ea[1];
			ea[0] = pitch;
			ea[1] = yaw;

			static DWORD s_log = 0;
			const DWORD now = GetTickCount();
			if (now - s_log >= 1000)
			{
				s_log = now;
				char buf[192];
				sprintf_s(buf, sizeof(buf), "[aa-mdl] pawn=%p before=%.1f/%.1f wrote=%.1f/%.1f read=%.1f/%.1f\n",
					pawn, before_pitch, before_yaw, pitch, yaw, ea[0], ea[1]);
				OutputDebugStringA(buf);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { }
	}

	// 复刻游戏 GameTraceLine 调用现场（client 0x84E340..0x84E36E，wrapper 全解码）：
	//   GameTraceLine([0x221F058]=mgr, &attrs, &start, &end, a5=0, a6=1, a7=&out[0xC0])
	//   attrs = 16B：0x203C30 构造 = desc(0x1ABC9C8, 实测全零)12B 拷贝 + float@+0xC
	//         （wrapper 走 xmm2=0 分支，等价内联）
	//   out 预置 = 0 清零 + [+0x6C]=-1(dword) + [+0xB0]=-1(qword) + fraction 默认 1
	//         （游戏 0x84E2BF..0x84E2E5 段同款，另设 [0x74]=0x07000000 —— 不设也照抄结果）
	// GameTraceLine 内部（0xA19AE0 反汇编实锤）：碰撞掩码 0x0F00FFFF 硬编码，
	// 构造 local query（vtable 0x1ACE848）→ 0xA0FB60 → 0xA19650(mgr, attrs, start, end, &local, out)。
	// 返回 fraction（out+0xAC，wrapper 0x84E3FA comiss [rbp+0x2C] 实锤），
	// 出参 hit_ent = out+0x08 命中实体指针（14189 实锤沿用）；hit byte = out+0xBB（0x84E400 实锤）。
	// 任何故障按"无命中"处理（trace 失败不废锁人）。纯 POD 局部，无 C++ 对象（C2712 约束）。
	// 旧 funnel 配方已废弃：14190 的 0x19F4220 实为 __chkstk（调它=空转，trace 全 miss 根因）。
	__declspec(noinline) float aw_trace_query(const float start[3], const float end[3], const void** hit_ent)
	{
		*hit_ent = nullptr;
		const auto base = reinterpret_cast<uintptr_t>(game->client.handle);
		if (!base)
			return 1.f;

		// [14191 已验 2026-10-18] trace 系 RVA 已全量重扫（tools/trace_rescan_14191.py：
		// pattern 唯一命中 + wrapper 调用点 +0xEE/+0xC0 同位 + singleton 149 xref + RTTI vtable 全名匹配）
		constexpr bool aw_trace_ready = true;
		if (!aw_trace_ready)
			return 1.f;

		float frac = 1.f;
		alignas(16) unsigned char out[0xC0] = {};

		// [2026-10-20 实测] 裸调 GameTraceLine 每帧静默 miss（frac 恒 1、hit 恒 null，无异常、mgr 非空）。
		// 反汇编 0x84EDB0 确认：游戏自己的高层 wrapper 调 GameTraceLine 前会先 CGameTrace::Init(out)
		// 并预置 out+0x74=0x07000000，再按 desc 构造 attrs（=0x203FA0 拷贝 12B + float）。
		// 直接走该 wrapper（rcx=start, rdx=end, xmm2=0, r9=out_copy），由游戏负责全部预置。
		__try
		{
			using wrapper_fn = bool(__fastcall*)(const float*, const float*, float, void*);
			reinterpret_cast<wrapper_fn>(base + sdk::offsets::functions::client::trace_wrapper)(
				start, end, 0.f, out);

			frac = *reinterpret_cast<const float*>(out + 0xAC);
			*hit_ent = *reinterpret_cast<void* const*>(out + 0x08);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			frac = 1.f;
			*hit_ent = nullptr;
		}

		return frac;
	}

	// [aimbot 可见性] 眼位→目标点视线判定。判定链：
	// ① 无命中（fraction≥0.99 且无实体）→ 可见；
	// ② 命中实体就是目标 pawn 自己 → 可见（骨骼点在头部 hitbox 内部，trace 停在目标表面）；
	// ③ frac≤0.05 的命中 → 可见（起点内命中：眼位在本地碰撞盒内部，trace 出发即撞
	//    自己——实测 frac=0.000~0.017、hit=世界对象、与朝向无关；真墙实测 frac≥0.38）；
	// ④ 其余（命中远墙/别人）→ 不可见。
	bool aw_visible_check(const float start[3], const float end[3], const void* target_pawn)
	{
		const void* hit = nullptr;
		const auto frac = aw_trace_query(start, end, &hit);
		if (!hit)
			return frac >= 0.99f;
		if (hit == target_pawn)
			return true;
		return frac <= 0.05f;
	}

	void prediction_update(void *pred, int a, int b)
	{
		//hook_manager.prediction_update->call(pred, a, b);
	}

	void* create_move(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7)
	{
		const char* phase = nullptr;
		const unsigned char* btn_host = static_cast<const unsigned char*>(a0);
		{
			const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			if (btn2_refractory > 0)
				--btn2_refractory;

			if (lmb && !btn_prev_lmb && btn_win_n < 0 && btn2_refractory == 0)
			{
				btn_win_n = 0;
				btn2_rel = false;
			}
			btn_prev_lmb = lmb;

			if (btn_win_n >= 0)
			{
				if (!lmb && !btn2_rel)
				{
					btn2_rel = true;
					btn2_rel_n = btn_win_n;
					phase = "rel";
				}
				else if (lmb && btn_win_n == 4)
				{
					phase = "held";
				}
				else if (btn2_rel && btn_win_n - btn2_rel_n >= 128)
				{
					phase = "idle";
					btn_win_n = -2;
					btn2_rel = false;
					btn2_refractory = 128;
				}
				++btn_win_n;
			}
		}

		// create_move 的 a0 = CCSGOInput 对象本体（14189 [host] 实锤 client+0x2572460，
		// 14190 磁盘反汇编同构）：+0x688 实时视角槽（dwViewAngles 相对偏移三代不变）。
		if (a0)
		{
			__try
			{
				aimbot_t::run(static_cast<sdk::ccsgo_input*>(a0));
				// [anti-aim] the local view slot (a0+0x688) must stay untouched:
				// real AA keeps your camera normal and only fakes the CUserCmd angle.
				// ForceButtonsDown 主动注入 / 解除检查（armed→注入，!armed→重放清位）
				autofire_inject_check();
				autofire_release_check();
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { }
		}

		const auto oCreateMove = hkCreateMove.GetOriginal();
		const auto ret = oCreateMove(a0, a1, a2, a3, a4, a5, a6, a7);

		
		if (phase && btn_host)
		{
			unsigned long long tblv = 0;
			if (btn_seh_copy(btn_host + 0xBD0, 8, reinterpret_cast<unsigned char*>(&tblv)) && tblv)
			{
				const unsigned char* tbl = reinterpret_cast<const unsigned char*>(tblv);
				unsigned char buf[0x60];
				if (btn_seh_copy(tbl, static_cast<int>(sizeof(buf)), buf))
				{
					
				}
			}
		}

		
		if (sdk::LocalPlayerController)
		{
			static bool s_af2_prev = false;
			const bool armed = aimbot_t::wants_autofire;
			{
				__try
				{
					using mgr_fn = void*(__fastcall*)(void*);
					const auto ring = reinterpret_cast<unsigned char*>(
						//client_module.value().base + 0x945360 =GetUserCmdManager [14192]（旧 0x944530 +0xE30）41 56 41 57 48 83 EC 48 48 8D 54 24 68 E8 AE B4
						reinterpret_cast<mgr_fn>(client_module.value().base + 0x945360)(sdk::LocalPlayerController)
					);
					if (ring)
					{
						
						const int seq = *reinterpret_cast<const int*>(ring + 0x5910);
						const auto cmd = ring + 0x98 * (static_cast<unsigned>(seq) % 150);
						if (*reinterpret_cast<const int*>(cmd + 0x8) == seq)
						{
							auto* const held = reinterpret_cast<unsigned long long*>(cmd + 0x60);
							auto* const changed = reinterpret_cast<unsigned long long*>(cmd + 0x68);
							if (aimbot_t::wants_autostop) {
								auto old_held = *held;
								*held &= ~0x618ull;
								*changed |= (old_held & 0x618);
							}
							//W 0x8 ,S 0x10,A 0x200,D 0x400
							if (armed)
							{
								
								
									*held |= 1ull;
									*changed |= 1ull;
								
								
								
								
							}
							else if (s_af2_prev)//停火
							{
								*held &= ~1ull;
								*changed |= 1ull;
							}
							else
							{
								*held &= ~1ull;
							}
						}
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER) { }
			}
			s_af2_prev = armed;
		}

		return ret;
	}

	__int64 myTraceLineIntervals(__int64* a1, int* out, __int64 a3, __int64 a4, BYTE* a5, int a6)
	{
		
		auto res=hkTraceLineIntervals.GetOriginal()(a1, out, a3, a4, a5, a6);

		auto* cur_out = reinterpret_cast<cutl_vector<sdk::trace_interval_t>*>(out);

		
		

		static uintptr_t s_last_out = 0;
		static int       s_last_size = -1;
		static DWORD     s_last_tick = 0;

		const DWORD now = GetTickCount();
		const bool out_changed = reinterpret_cast<uintptr_t>(out) != s_last_out;
		const bool size_changed = cur_out->size != s_last_size;
		const bool time_ready = (now - s_last_tick) >= 500;

		// 只在 out/size 变化、且距离上次至少 500ms 时才打，避免刷屏
		if ((out_changed || size_changed) && time_ready)
		{
			s_last_out = reinterpret_cast<uintptr_t>(out);
			s_last_size = cur_out->size;
			s_last_tick = now;

			char hdr[192];
			sprintf_s(hdr, sizeof(hdr),
				"[TLI] out=%p size=%d elements=%p\n",
				out, cur_out->size, cur_out->elements);
			//OutputDebugStringA(hdr);

			// 最多只打前 8 项，size 很大时避免一次几百行
			const int dump_items = (cur_out->size < 8) ? cur_out->size : 8;

			for (int i = 0; i < dump_items; ++i)
			{
				const auto* base = reinterpret_cast<const unsigned char*>(cur_out->elements) + i * 56;
				for (int q = 0; q < 7; ++q)
				{
					unsigned long long v = 0;
					if (!aw_read_qword(base + q * 8, &v))
						break;

					const auto lo_u = static_cast<unsigned int>(v & 0xFFFFFFFFull);
					const auto hi_u = static_cast<unsigned int>(v >> 32);
					float lo_f = 0.f, hi_f = 0.f;
					std::memcpy(&lo_f, &v, 4);
					std::memcpy(&hi_f, reinterpret_cast<const char*>(&v) + 4, 4);

					char line[256];
					sprintf_s(line, sizeof(line),
						"  [%02d]+0x%02X %016llX  u32=[%08X %08X]  f=[% .4f % .4f]\n",
						i, q * 8, v, lo_u, hi_u, lo_f, hi_f);
					//OutputDebugStringA(line);
				}
			}

			if (cur_out->size > dump_items)
			{
				char tail[128];
				sprintf_s(tail, sizeof(tail),
					"  ... (%d more items omitted)\n",
					cur_out->size - dump_items);
				//OutputDebugStringA(tail);
			}
		}
		
		return res;
	}

	void* fill_cmd_angles(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7)
	{
		const auto oFill = hkFillCmdAngles.GetOriginal();
		const auto ret = oFill(a0, a1, a2, a3, a4, a5, a6, a7);

		// [silent aim] a1 = 命令上下文 rdx：[+0x18]=CUserCmd，+0x18/+0x1C=viewangles P/Y。
		// original 已填 live 视角并置 dirty 位（bit0/1/2），此处覆写为目标头角即可
		// （用户 NOP 实验证明这是 viewangles 唯一写入点，之后无覆盖）。
		if (a1)
		{
			__try
			{
				const auto cmd = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(a1) + 0x18);
				if (cmd)
				{
					if (aimbot_t::silent_valid)
					{
						*reinterpret_cast<float*>(cmd + 0x18) = aimbot_t::silent_pitch;
						*reinterpret_cast<float*>(cmd + 0x1C) = aimbot_t::silent_yaw;
						// 本地模型镜像出站角（silent 也只影响模型朝向，不动相机）；
						// AA 关闭时不产生任何本地视觉残留
						if (cfg.aa.enabled.get())
						{
							aa_model_pitch = aimbot_t::silent_pitch;
							aa_model_yaw = aimbot_t::silent_yaw;
							aa_model_valid = true;
						}
						else
						{
							aa_model_valid = false;
						}
					}
					else
					{
						// [anti-aim] fake the angle sent to the server (model facing) while
						// the local camera (a0+0x688) stays untouched. Skipped on firing
						// frames so bullets still leave along the real view.
						const bool aa_on = cfg.aa.enabled.get();
						if (!aa_on)
							aa_model_valid = false;

						const bool firing = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
							|| aimbot_t::wants_autofire;
						if (!firing)
						{
							float aa_pitch = *reinterpret_cast<float*>(cmd + 0x18);
							float aa_yaw = *reinterpret_cast<float*>(cmd + 0x1C);
							const bool ok = antiaim::apply(aa_pitch, aa_yaw);
							if (ok)
							{
								*reinterpret_cast<float*>(cmd + 0x18) = aa_pitch;
								*reinterpret_cast<float*>(cmd + 0x1C) = aa_yaw;
							}
							// [aa] 把这份假角存给 on_render_start，本地模型显示同一份角
							// （旧实现两处分别调 apply，渲染侧基准角恒为 0 → 模型朝向错误）。
							// 先写角再置 valid，避免渲染线程读到"valid 但角还没落"的中间态
							if (ok)
							{
								aa_model_pitch = aa_pitch;
								aa_model_yaw = aa_yaw;
							}
							aa_model_valid = ok;
							// [aa-dbg] confirm the AA path is reached and what it computes
							static DWORD s_aa_log = 0;
							const DWORD now = GetTickCount();
							if (now - s_aa_log >= 500)
							{
								s_aa_log = now;
								char buf[192];
								sprintf_s(buf, sizeof(buf), "[aa] en=%d ok=%d out=%.1f/%.1f\n",
									cfg.aa.enabled.get() ? 1 : 0, ok ? 1 : 0, aa_pitch, aa_yaw);
								OutputDebugStringA(buf);
							}
						}
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { }
		}

		
		

		return ret;
	}

	void frame_stage_notify(void *rcx, sdk::client_frame_stage stage)
	{
		

		//hook_manager.frame_stage_notify->call(rcx, stage);

	
	}

	void override_view(void *rcx, sdk::cview_setup *view_setup)
	{
		

		static float progress = 0.f;
		static bool was_on = false;

		const bool cfg_now = cfg.misc.thirdperson.get();
		static bool cfg_prev = false;
		const bool cfg_rising = cfg_now && !cfg_prev;
		cfg_prev = cfg_now;

		bool run_thirdperson = false;
		const void* dbg_ctrl = nullptr;
		bool dbg_alive = false;

		if (cfg_now)
		{
			// GlobalVars/LocalPlayerController 每帧重读（切图/大厅重写槽位，缓存指针悬垂）
			if (game->client.handle)
				sdk::GlobalVars = reinterpret_cast<sdk::global_vars_t*>(game->client.deref(sdk::offsets::globals::global_vars));

			__try
			{
				if (const auto client_base = static_cast<const uint8_t*>(MEM::GetModuleBaseHandle(CLIENT_DLL)))
					sdk::LocalPlayerController = *reinterpret_cast<sdk::cs2_player_controller* const*>(
						client_base + sdk::offsets::globals::local_player_controller);

				if (const auto ctrl = sdk::LocalPlayerController)
				{
					dbg_ctrl = ctrl;
					dbg_alive = ctrl->get_pawn_is_alive();
					run_thirdperson = dbg_alive || cfg.misc.thirdperson_dead.get();
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { }
		}

		const auto bezier = [](const float t) { return t * t * (3.0f - 2.0f * t); };

		if (run_thirdperson)
		{
			// [14190] 进第三人称：直接调 thirdperson ConCommand 回调（原生全流程：视角快照+
			// 状态位+vcall+0x9D8(1)）。必须【每帧重放】不能只在勾选沿调一次——实测 9D8 置位
			// 只活一帧就被游戏相机/输入主循环复位（症状：视角闪一下回第一人称）；回调自带
			// 幂等门（vcall+310 查到已 TP 则 no-op），被复位帧会自动重新夺位，重生后也能自愈
			if (const auto client_base = reinterpret_cast<uintptr_t>(MEM::GetModuleBaseHandle(CLIENT_DLL)))
			{
				__try
				{
					reinterpret_cast<void(__fastcall*)()>(client_base + sdk::offsets::functions::client::cmd_thirdperson)();
				}
				__except (EXCEPTION_EXECUTE_HANDLER) { }
			}

			// var() 无 static：cvar 系统重建（切图/大厅）会释放 convar 对象，static 缓存悬垂
			// 是大厅注入闪退根因（见 project 记忆）；find 本身 SEH 防护，调用方判空
			var(cam_idealdist);
			var(cam_collision);
			var(cam_snapto);
			var(c_thirdpersonshoulder);
			var(c_thirdpersonshoulderaimdist);
			var(c_thirdpersonshoulderdist);
			var(c_thirdpersonshoulderheight);
			var(c_thirdpersonshoulderoffset);

			const float dist = cfg.misc.thirdperson_dist.get();

			// 拉远动画：40/dist 下限 = 从 40u 起步，bezier 缓动到目标距离；no_interp = 禁用动画直取
			if (cfg.misc.thirdperson_no_interp.get() || !sdk::GlobalVars)
				progress = 1.f;
			else
				progress = clamp(progress + sdk::GlobalVars->frame_time * 6.f, 40.f / dist, 1.f);

			if (cam_idealdist) cam_idealdist->value.fl = dist * (cfg.misc.thirdperson_no_interp.get() ? 1.f : bezier(progress));
			if (cam_collision) cam_collision->value.i1 = true;
			if (cam_snapto) cam_snapto->value.i1 = true;
			if (c_thirdpersonshoulder) c_thirdpersonshoulder->value.i1 = true;
			// 肩部偏移全清零 = 纯背后居中视角
			if (c_thirdpersonshoulderaimdist) c_thirdpersonshoulderaimdist->value.fl = 0.f;
			if (c_thirdpersonshoulderdist) c_thirdpersonshoulderdist->value.fl = 0.f;
			if (c_thirdpersonshoulderheight) c_thirdpersonshoulderheight->value.fl = 0.f;
			if (c_thirdpersonshoulderoffset) c_thirdpersonshoulderoffset->value.fl = 0.f;

			// [14190] 相机实际距离 = 状态单例 [client+tp_state_singleton] 对象 +0x238。
			// cam_idealdist 不被相机消费（拖到 180 相机仍钉在首次激活硬编码的 30.0f），
			// 每帧直写覆盖，值与 convar 写入同步（含 bezier 缓动/no_interp）
			{
				unsigned long long obj = 0;
				if (const auto client_base = reinterpret_cast<uintptr_t>(MEM::GetModuleBaseHandle(CLIENT_DLL)))
				{
					__try
					{
						obj = *reinterpret_cast<unsigned long long*>(client_base + sdk::offsets::functions::client::tp_state_singleton);
						if (obj > 0x10000 && obj < 0x7FFFFFFFFFFFull)
							*reinterpret_cast<float*>(obj + 0x238) = dist * (cfg.misc.thirdperson_no_interp.get() ? 1.f : bezier(progress));
					}
					__except (EXCEPTION_EXECUTE_HANDLER) { }
				}
			}

			was_on = true;

			// [tp-probe] 勾选沿一次性快照（wsprintfA 无 %f，浮点走 sprintf_s）
			if (cfg_rising)
			{
				char szBuffer[224];
				sprintf_s(szBuffer, sizeof(szBuffer),
					"[tp] on: ctrl=%p alive=%d ideal=%p fl=%.1f shoulder=%p i1=%d dist=%.1f gv=%p\n",
					dbg_ctrl, dbg_alive ? 1 : 0,
					static_cast<const void*>(cam_idealdist), cam_idealdist ? cam_idealdist->value.fl : -1.f,
					static_cast<const void*>(c_thirdpersonshoulder), c_thirdpersonshoulder ? static_cast<int>(c_thirdpersonshoulder->value.i1) : -1,
					dist, static_cast<void*>(sdk::GlobalVars));
				OutputDebugStringA(szBuffer);
			}
		}
		else if (cfg_rising)
		{
			// [tp-probe] 勾选了但未激活（未进局/死亡未强制）—— 判定结果可见
			char szBuffer[128];
			wsprintfA(szBuffer, "[tp] cfg on but inactive: ctrl=%p alive=%d dead_force=%d\n",
				dbg_ctrl, dbg_alive ? 1 : 0, cfg.misc.thirdperson_dead.get() ? 1 : 0);
			OutputDebugStringA(szBuffer);
		}
		else if (was_on)
		{
			// 关闭瞬间恢复第一人称：调 firstperson ConCommand 回调（原生复位：状态标志+
			// vcall+0x9D8(0)+FOV 恢复）；convar 复位保留兜底。只走一次（was_on 门闩），
			// 大厅/主菜单从未开启时完全不碰
			if (const auto client_base = reinterpret_cast<uintptr_t>(MEM::GetModuleBaseHandle(CLIENT_DLL)))
			{
				__try
				{
					reinterpret_cast<void(__fastcall*)()>(client_base + sdk::offsets::functions::client::cmd_firstperson)();
				}
				__except (EXCEPTION_EXECUTE_HANDLER) { }
			}

			var(c_thirdpersonshoulder);
			if (c_thirdpersonshoulder) c_thirdpersonshoulder->value.i1 = false;
			was_on = false;
		}

		hkOverrideView.GetOriginal()(rcx, view_setup);
	}

	float get_fov(void *rcx)
	{
		/*if (cfg.misc.fov_enabled.get())
			return cfg.misc.fov.get();

		return hook_manager.get_fov->call(rcx);*/
		return 0.f;
	}

	void on_render_start(sdk::cview_render *view_render)
	{
		// [anti-aim] 本地第三人称模型朝向：直接复用本 tick 已写入 CUserCmd 的那份假角
		// （fill_cmd_angles 以真实视角为基准算出的绝对值）。绝不在这里再调 apply——
		// 渲染侧没有真实基准角（旧实现传 0/0，Backwards 恒输出 180°，模型朝向错误），
		// 且每帧多调一次会打乱 jitter 的 tick 相位。
		// 必须在 original 之前写：original 会跑客户端动画/姿态更新，写在之后只落下一帧，
		// 还会被下一帧预测覆盖（旧实现"apply(0,0) + original 之后写"= 模型不转的根因）。
		if (aa_model_valid)
			aa_write_local_model(aa_model_pitch, aa_model_yaw);

		const auto oOnRenderStart = hkOnRenderStart.GetOriginal();

		oOnRenderStart(view_render);
	/*
		if (menu::init_anim.init_done && evo::ren::draw.adapter && draw_mgr.buf->vb->object && !draw_mgr.buf->is_deferred_ready)
		{
			const bool was_dirty = draw_mgr.buf->deferred_dirty;
			if (!was_dirty && !draw_mgr.dirty_ackn)
			{
				draw_mgr.buf->reset(true);
				draw_mgr.buf->lock();

				visuals.draw_debug_info();

				visuals.run(&view_render->setup);

				if (menu::men.finalized)
				{
					const auto ctx = static_cast<ID3D11DeviceContext*>(evo::ren::draw.adapter->get_deferred_context());
					ctx->ClearState();

					const auto cmd_list = reinterpret_cast<ID3D11CommandList**>(evo::ren::draw.adapter->get_deferred_list());
					if (*cmd_list)
						(*cmd_list)->Release();

					draw_mgr.buf->unlock();

					ctx->FinishCommandList(false, cmd_list);
					draw_mgr.buf->is_deferred_ready = true;
				}

			}
			else
			{
				if (was_dirty)
					draw_mgr.dirty_ackn = true;
				draw_mgr.buf->is_deferred_ready = true;
			}
		}*/
	}

	// [aw-verify] 同步 trace 漏斗（client RVA 0xA1A890）单次 dump：
	// F5 在 present 中置位 armed → 抓到「命中」trace（fraction∈(0,1) 或 hit==1）
	// 才消费 dump：8 个参数槽 + vtable 归属 + surface/句柄/endpos/normal 布局字段。
	// 未命中 trace（fraction=1.0，surface=默认填充）没有验证价值，直接放行。
	// 全程只读，零游戏函数调用。首测实锤：出参 = 第 7 参数（arg6，栈传）。
	void* trace_funnel(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7)
	{
		// 调用计数：armed 消费时打印，present 的 3s 警告也读取（判断漏斗冷/热）
		const auto calls = trace_funnel_calls.fetch_add(1, std::memory_order_relaxed) + 1;

		const auto oTraceFunnel = hkTraceFunnel.GetOriginal();
		if (!oTraceFunnel)
			return nullptr;

		// 注意：trace API 全部是指针/整型参数，无 xmm 传参，透传安全
		const auto result = oTraceFunnel(a0, a1, a2, a3, a4, a5, a6, a7);
		return result;
	}

	// [aw-verify] trace 展开器（RVA 0x9D31A0）：实测对局内极热（首个 dump 时已 5 万+ 次调用），
	// 漏斗冷时的主捕获点。8 槽透传（漏斗教训：出参可能在第 7/8 参数，栈传）。
	// 第三轮实测：命中判据抓到的全是「打墙背景探针」——7 发全部 surface=默认，
	// ret≡float(endpos.z)|1（同一调用现场家族的“命中高度”返回值），hit(+BB) 恒 0
	// —— 可见性/落地/脚步类 trace，不是子弹。故加 filter vtable 门控：只有 args
	// 中出现 CCSTraceFilterSimple（0x8C931B 子弹使用现场实锤的 filter）才消费 armed；
	// 无已知 filter 的调用现场仅限流诊断（若始终无 filter → 子弹不经此展开器，
	// 转钩 0x8C931B 所在函数）。
	void* trace_expander(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7)
	{
		const auto calls = trace_expander_calls.fetch_add(1, std::memory_order_relaxed) + 1;

		const auto oExpander = hkTraceExpander.GetOriginal();
		if (!oExpander)
			return nullptr;

		const auto result = oExpander(a0, a1, a2, a3, a4, a5, a6, a7);
		return result;
	}

	// [aw-verify] 子弹 trace 封装（RVA 0x8C931B，.pdata 实证函数起始=CCSTraceFilterSimple
	// 构造现场，0x8C931B..0x8C962B）。实测子弹不经漏斗（开枪 funnel=0）、不经展开器
	//（armed 期间 8 槽从未出现 filter）→ 第三捕获点 = 直接钩本函数。
	// armed 时 8 槽做命中型判定，命中即消费 armed 并 dump（args 注解 + CGameTrace 字段）；
	// 每 2048 次调用限流打印一行诊断（观察本函数冷热与出参位置）。
	void* bullet_trace_helper(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7)
	{
		const auto calls = bullet_helper_calls.fetch_add(1, std::memory_order_relaxed) + 1;

		const auto oHelper = hkBulletTrace.GetOriginal();
		if (!oHelper)
			return nullptr;

		const auto result = oHelper(a0, a1, a2, a3, a4, a5, a6, a7);
		return result;
	}

	// [aw-verify] 11 个 filter dtor 包装：原调用后上报事件（self + 返回地址）。
	// scalar-deleting-dtor 原型：void* this(void* this, uchar free_flag)，返回 this。
#define AW_DEFINE_FILTER_DTOR(ID, SFX)                                                                          \
	void* aw_filter_dtor##SFX(void* self, unsigned char free_flags)                                           \
	{                                                                                                          \
		void* ret = self;                                                                                      \
		const auto o = hkFilterDtor##SFX.GetOriginal();                                                      \
		if (o)                                                                                                 \
			ret = reinterpret_cast<void* (*)(void*, unsigned char)>(o)(self, free_flags);                     \
		aw_filter_dtor_event(ID, self, _ReturnAddress());                                                     \
		return ret;                                                                                           \
	}

	AW_DEFINE_FILTER_DTOR(0, 00)
	AW_DEFINE_FILTER_DTOR(1, 01)
	AW_DEFINE_FILTER_DTOR(2, 02)
	AW_DEFINE_FILTER_DTOR(3, 03)
	AW_DEFINE_FILTER_DTOR(4, 04)
	AW_DEFINE_FILTER_DTOR(5, 05)
	AW_DEFINE_FILTER_DTOR(6, 06)
	AW_DEFINE_FILTER_DTOR(7, 07)
	AW_DEFINE_FILTER_DTOR(8, 08)
	AW_DEFINE_FILTER_DTOR(9, 09)
	AW_DEFINE_FILTER_DTOR(10, 10)
#undef AW_DEFINE_FILTER_DTOR

		
	void force_buttons_down(void* a0, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7)
	{
		const auto oForce = hkForceButtonsDown.GetOriginal();
		s_fbd_dev = a0;
		s_fbd_mask = reinterpret_cast<unsigned long long>(a1);
		// [fbd] 探针：原始位集变化时 dump（物理 LMB 按下/松开即翻位 → 定位 IN_ATTACK 位号）
		static unsigned long long s_prev_mask = ~0ull;
		if (s_fbd_mask != s_prev_mask)
		{
			char buf[96];
			sprintf_s(buf, sizeof(buf), "Fatality: [fbd] mask=0x%016llx\n", s_fbd_mask);
			OutputDebugStringA(buf);
			s_prev_mask = s_fbd_mask;
		}
		s_fbd_x[0] = a2; s_fbd_x[1] = a3; s_fbd_x[2] = a4;
		s_fbd_x[3] = a5; s_fbd_x[4] = a6; s_fbd_x[5] = a7;
		if (aimbot_t::wants_autofire)
		{
			a1 = reinterpret_cast<void*>(s_fbd_mask | 1ULL); // IN_ATTACK = button code 0（btnhex 按下沿事件字段=0 实证）
			s_fbd_injected = true;
			static bool logged = false;
			if (!logged)
			{
				OutputDebugStringA("Fatality: [fbd] IN_ATTACK injected\n");
				logged = true;
			}
		}
		oForce(a0, a1, a2, a3, a4, a5, a6, a7);
	}

	// create_move 钩子调用：武装帧主动调 ForceButtonsDown（游戏自身只在清空位集时
	// 才调它 —— 两个调用点均 xor edx,edx，被动钩子永远等不到注入窗口，磁盘反汇编实锤）。
	// a1 = 设备当前按下态(+0x190) | IN_ATTACK：基于当前态合成，物理按键位不受破坏；
	// +0x190 是允许掩码过滤后的按下态镜像（sub_A66050 内 [rbx+190h] 全量覆盖写），幂等安全
	__declspec(noinline) static auto fbd_read_state(const void* dev) -> unsigned long long
	{
		__try { return *reinterpret_cast<const unsigned long long*>(static_cast<const unsigned char*>(dev) + 0x190); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	void autofire_inject_check()
	{
		if (!aimbot_t::wants_autofire || s_fbd_injected || !s_fbd_dev)
			return;
		const auto cur = fbd_read_state(s_fbd_dev);
		hkForceButtonsDown.GetOriginal()(s_fbd_dev, reinterpret_cast<void*>(cur | 1ULL),
			s_fbd_x[0], s_fbd_x[1], s_fbd_x[2], s_fbd_x[3], s_fbd_x[4], s_fbd_x[5]);
		s_fbd_injected = true;
		
	}

	

	

	// create_move 钩子调用：解除帧（目标丢失/不可见/复选框关）重放原始掩码清注入位，
	// 物理按键位保留（全量覆盖语义下本次重放=精确撤销注入，无隔墙余弹）
	void autofire_release_check()
	{
		if (!s_fbd_injected || aimbot_t::wants_autofire || !s_fbd_dev)
			return;
		const auto oForce = hkForceButtonsDown.GetOriginal();
		oForce(s_fbd_dev, reinterpret_cast<void*>(s_fbd_mask & ~1ULL),
			s_fbd_x[0], s_fbd_x[1], s_fbd_x[2], s_fbd_x[3], s_fbd_x[4], s_fbd_x[5]);
		s_fbd_injected = false;
		
	}

}




