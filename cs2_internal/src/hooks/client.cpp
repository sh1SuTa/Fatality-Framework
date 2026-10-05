#include <cstring>
#include <cmath>
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

	struct aw_stack_trace_cand { const unsigned char* p; float fraction; int hit; };

	__declspec(noinline) void aw_filter_dtor_event(int id, const void* self, const void* retaddr)
	{
		hooks::client::filter_dtor_calls[id].fetch_add(1, std::memory_order_relaxed);

		// 每次 F5 重新武装时清零日志预算（dtor 很热，事件持续触发可观察边沿）
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
	// [aimbot 可见性] 可复用同步 trace 段（配方同 aw_self_trace_test，p5=1 命中挡位）。
	// 返回 fraction，出参 hit_ent = 命中实体指针（CGameTrace+0x08，miss 时为 null）；
	// 任何故障按"无命中"处理（trace 失败不废锁人）。
	// 纯 POD 局部，无 C++ 对象（C2712 约束）。
	__declspec(noinline) float aw_trace_query(const float start[3], const float end[3], const void** hit_ent)
	{
		*hit_ent = nullptr;
		const auto base = reinterpret_cast<uintptr_t>(game->client.handle);
		if (!base)
			return 1.f;
		const auto phys = *reinterpret_cast<void**>(base + sdk::offsets::functions::client::physics_query_singleton);
		if (!phys)
			return 1.f;

		__try
		{
			using ctor_fn = void* (__fastcall*)(void*, const void*);
			alignas(16) unsigned char filter[0x60] = {};
			const auto attrs = reinterpret_cast<ctor_fn>(base + sdk::offsets::functions::client::trace_filter_ctor)(
				filter, reinterpret_cast<const void*>(base + sdk::offsets::functions::client::trace_filter_desc));
			if (!attrs)
				return 1.f;

			unsigned char qdesc[0x30] = {};
			std::memcpy(qdesc, attrs, 16);
			qdesc[0x28] = (*reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(attrs) + 0xC) > 0.f) ? 1 : 0;

			alignas(16) unsigned char out[0xC0] = {};
			*reinterpret_cast<float*>(out + 0xAC) = 1.f;                 // fraction
			*reinterpret_cast<std::uint32_t*>(out + 0x68) = 0xffffffffu; // handle
			*reinterpret_cast<float*>(out + 0x3C) = 1.f;                 // dmgmod

			float s[3] = {start[0], start[1], start[2]};
			float e[3] = {end[0], end[1], end[2]};

			using funnel_fn = void* (__fastcall*)(void*, void*, void*, void*, void*, void*, void*);
			reinterpret_cast<funnel_fn>(base + sdk::offsets::functions::client::trace_funnel)(
				phys, qdesc, s, e, nullptr, reinterpret_cast<void*>(static_cast<uintptr_t>(1)), out);

			const auto frac = *reinterpret_cast<const float*>(out + 0xAC);
			*hit_ent = *reinterpret_cast<void* const*>(out + 0x08);
			return frac;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return 1.f;
		}
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

	// [aw-verify] 自调用同步 trace（F5 触发）—— 配方复刻 client 0x80CE65 现场（build 14188）：
	// funnel(rcx=单例, rdx=&qdesc, r8=&start, r9=&end, [20]=NULL, [28]=1, [30]=&out)
	// 单例=[client+0x2223148]；filter=0x203d00(&F, desc@0x1ABF9C8) 返回属性结构；
	// qdesc=16B@[rax] 拷贝 + qdesc+0x28 byte=(float[rax+0xC]>0)；
	// 出参默认态 = CGameTrace::Init 实测（fraction=1, handle=-1, dmgmod=1）
	// 眼位读取与 visuals 的 read_local_eye_seh 同链（LocalPlayerController→pawn→node）
	__declspec(noinline) void aw_self_trace_test()
	{
		const auto base = reinterpret_cast<uintptr_t>(game->client.handle);
		if (!base)
			return;

		const auto phys = *reinterpret_cast<void**>(base + sdk::offsets::functions::client::physics_query_singleton);
		if (!phys)
			return;

		// SEH 保护的本地眼位/视向（同 visuals::read_local_eye_seh 链，pawn 可能悬垂）
		float eye[3] = {};
		float yaw = 0.f;
		__try
		{
			const auto ctrl = sdk::LocalPlayerController;
			if (!ctrl)
				return;
			const auto pawn = ctrl->get_pawn();
			if (!pawn)
				return;
			const auto node = pawn->get_m_pGameSceneNode();
			if (!node)
				return;
			const auto org = node->get_vec_abs_origin();
			eye[0] = org.x; eye[1] = org.y; eye[2] = org.z;
			eye[2] += pawn->get_m_vecViewOffset().get_vec_z();
			yaw = pawn->get_v_angle().y;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return;
		}

		using ctor_fn = void* (__fastcall*)(void*, const void*);
		const auto ctor = reinterpret_cast<ctor_fn>(base + sdk::offsets::functions::client::trace_filter_ctor);
		alignas(16) unsigned char filter[0x60] = {};
		const auto attrs = ctor(filter, reinterpret_cast<const void*>(base + sdk::offsets::functions::client::trace_filter_desc));
		if (!attrs)
			return;

		unsigned char qdesc[0x30] = {};
		std::memcpy(qdesc, attrs, 16);
		qdesc[0x28] = (*reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(attrs) + 0xC) > 0.f) ? 1 : 0;

		using funnel_fn = void* (__fastcall*)(void*, void*, void*, void*, void*, void*, void*);
		const auto funnel = reinterpret_cast<funnel_fn>(base + sdk::offsets::functions::client::trace_funnel);

		// yaw 正方向=左转（oof 方向链同源），水平前向 dir={-sin,-cos}
		const float rad = yaw * 3.14159265f / 180.f;

		// 探针组（第六轮已定案：p5=1/3 命中路径、0/2 miss；fraction=0..1×ray_len；
		// +0x84=真 endpos、+0x78=start 拷贝、+0xAC=fraction、+0xBC=hit bool）
		struct aw_probe { const char* name; float delta[3]; uintptr_t p5; };
		const float fwd_x = -std::sin(rad) * 256.f, fwd_y = -std::cos(rad) * 256.f;
		const aw_probe probes[] = {
			{ "fwd p5=1", { fwd_x, fwd_y, 0.f }, 1 },
			{ "down p5=1", { 0.f, 0.f, -128.f }, 1 },
		};

		for (const auto& pr : probes)
		{
			// 出参（0xC0）：按 expander dump 实测默认态预填，funnel 命中后覆写
			alignas(16) unsigned char out[0xC0] = {};
			*reinterpret_cast<float*>(out + 0xAC) = 1.f;                 // fraction
			*reinterpret_cast<std::uint32_t*>(out + 0x68) = 0xffffffffu; // entity handle
			*reinterpret_cast<float*>(out + 0x3C) = 1.f;                 // dmgmod 默认

			float start[3] = { eye[0], eye[1], eye[2] };
			float end[3] = { eye[0] + pr.delta[0], eye[1] + pr.delta[1], eye[2] + pr.delta[2] };
			funnel(phys, qdesc, start, end, nullptr, reinterpret_cast<void*>(pr.p5), out);

			// [aw-verify 已收口] hex/字段/surface32 日志全部静音（代码保留备查，需要时恢复）
			(void)out;
		}
	}

	void prediction_update(void *pred, int a, int b)
	{
		//hook_manager.prediction_update->call(pred, a, b);
	}

	void* create_move(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7)
	{
		// 8 槽透传（栈参数 5-8 由 MSVC 写进本函数的 outgoing area）。
		// 先跑 aimbot（silent 目标角/非 silent 视角写入都在 original 之前就绪），
		// 再让原函数生成当前命令——original 内部会调 fill_cmd_angles（已钩），
		// silent 目标角在本 tick 即被消费。

		// [btn2] 相位检测（pre-original）+ 表 dump（post-original）。
		// 上轮实测：a0 恒为 0x7FFC... 模块地址、+0xBD0 始终有表、cmd 递增 = 宿主 r15。
		// 但 pre-original 读 pressed 全 -1（上帧重置后）—— 按钮态在 create_move 内部
		// switch case 写入，必须 original 之后读。
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

		if (a0)
		{
			__try { aimbot_t::run(static_cast<sdk::ccsgo_input*>(a0)); }
			__except (EXCEPTION_EXECUTE_HANDLER) { }
		}

		const auto oCreateMove = hkCreateMove.GetOriginal();
		const auto ret = oCreateMove(a0, a1, a2, a3, a4, a5, a6, a7);

		// [btn2] post-original：hex dump a0 表 entry 0 全 0x60 字节。
		// e0-e3 pressed 恒 -1，说明 +0x58 不是按下态或表被重置。
		// 逐字节 diff held vs idle，找真正翻转的偏移。
		if (phase && btn_host)
		{
			unsigned long long tblv = 0;
			if (btn_seh_copy(btn_host + 0xBD0, 8, reinterpret_cast<unsigned char*>(&tblv)) && tblv)
			{
				const unsigned char* tbl = reinterpret_cast<const unsigned char*>(tblv);
				unsigned char buf[0x60];
				if (btn_seh_copy(tbl, static_cast<int>(sizeof(buf)), buf))
				{
					char line[512];
					for (int row = 0; row < 6; ++row)
					{
						int pos = snprintf(line, sizeof(line), "Fatality: [btn2] %s e0 %02x:", phase, row * 16);
						for (int i = 0; i < 16 && pos > 0 && pos < static_cast<int>(sizeof(line)) - 4; ++i)
							pos += snprintf(line + pos, sizeof(line) - static_cast<size_t>(pos),
								" %02x", buf[row * 16 + i]);
						if (pos > 0)
						{
							snprintf(line + pos, sizeof(line) - static_cast<size_t>(pos), "\n");
							OutputDebugStringA(line);
						}
					}
				}
				else
				{
					OutputDebugStringA("Fatality: [btn2] e0 copy fail\n");
				}
			}
			else
			{
				char line[64];
				snprintf(line, sizeof(line), "Fatality: [btn2] %s a0tbl=null\n", phase);
				OutputDebugStringA(line);
			}
		}

		return ret;
	}

	void* fill_cmd_angles(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7)
	{
		const auto oFill = hkFillCmdAngles.GetOriginal();
		const auto ret = oFill(a0, a1, a2, a3, a4, a5, a6, a7);

		// [silent aim] a1 = 命令上下文 rdx：[+0x18]=CUserCmd，+0x18/+0x1C=viewangles P/Y。
		// original 已填 live 视角并置 dirty 位（bit0/1/2），此处覆写为目标头角即可
		// （用户 NOP 实验证明这是 viewangles 唯一写入点，之后无覆盖）。
		if (a1 && aimbot_t::silent_valid)
		{
			__try
			{
				const auto cmd = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(a1) + 0x18);
				if (cmd)
				{
					*reinterpret_cast<float*>(cmd + 0x18) = aimbot_t::silent_pitch;
					*reinterpret_cast<float*>(cmd + 0x1C) = aimbot_t::silent_yaw;
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
		//const auto run_thirdperon = cfg.misc.thirdperson.get(); //TODO: check for cfg.misc.thirdperson_grenade.get();

		//var(cam_idealdist);
		//var(cam_collision);
		//var(cam_snapto);
		//var(c_thirdpersonshoulder);
		//var(c_thirdpersonshoulderaimdist);
		//var(c_thirdpersonshoulderdist);
		//var(c_thirdpersonshoulderheight);
		//var(c_thirdpersonshoulderoffset);

		//static auto progress = 0.f;
		//if (run_thirdperon)
		//{
		//	auto bezier = [](const float t) { return t * t * (3.0f - 2.0f * t); };

		//	progress = clamp(progress + sdk::GlobalVars->frame_time * 6.f, 40.f / cfg.misc.thirdperson_dist.get(), 1.f);

		//	cam_idealdist->value.fl = cfg.misc.thirdperson_dist.get() * (cfg.misc.thirdperson_no_interp.get() ? 1.f : bezier(progress));
		//	cam_collision->value.i1 = true;
		//	cam_snapto->value.i1 = true;
		//	c_thirdpersonshoulder->value.i1 = true;
		//	c_thirdpersonshoulderaimdist->value.fl = 0.f;
		//	c_thirdpersonshoulderdist->value.fl = 0.f;
		//	c_thirdpersonshoulderheight->value.fl = 0.f;
		//	c_thirdpersonshoulderoffset->value.fl = 0.f;

		//	game->input()->in_third_person = true;
		//}
		//else
		//{
		//	progress = cfg.misc.thirdperson.get() ? 1.f : 0.f;
		//	game->input()->in_third_person = false;
		//}


		//hook_manager.override_view->call(rcx, view_setup);
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

}




/*


pandora
onetap
gamesense
neverlose
fatality



dominance
satanophobia
*/