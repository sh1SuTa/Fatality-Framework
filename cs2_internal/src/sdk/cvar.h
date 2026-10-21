#pragma once
#include <cstring>
#include "color.h"
#include "macros.h"
#include "math/vector.h"
#include <memory/memory.h>
#include <sdk/offsets.h>

namespace sdk
{
	enum cvar_flags : int
	{
		FCVAR_NONE = 0,
		FCVAR_UNREGISTERED = (1 << 0),
		FCVAR_DEVELOPMENTONLY = (1 << 1),
		FCVAR_GAMEDLL = (1 << 2),
		FCVAR_CLIENTDLL = (1 << 3),
		FCVAR_HIDDEN = (1 << 4),
		FCVAR_PROTECTED = (1 << 5),
		FCVAR_SPONLY = (1 << 6),
		FCVAR_ARCHIVE = (1 << 7),
		FCVAR_NOTIFY = (1 << 8),
		FCVAR_USERINFO = (1 << 9),
		FCVAR_CHEAT = (1 << 14),
		FCVAR_PRINTABLEONLY = (1 << 10),
		FCVAR_UNLOGGED = (1 << 11),
		FCVAR_NEVER_AS_STRING = (1 << 12),
		FCVAR_REPLICATED = (1 << 13),
		// TODO: (1 << 14) used somehow
		FCVAR_DEMO = (1 << 16),
		FCVAR_DONTRECORD = (1 << 17),
		FCVAR_RELOAD_MATERIALS = (1 << 20),
		FCVAR_RELOAD_TEXTURES = (1 << 21),
		FCVAR_NOT_CONNECTED = (1 << 22),
		FCVAR_MATERIAL_SYSTEM_THREAD = (1 << 23),
		FCVAR_ARCHIVE_XBOX = (1 << 24),
		FCVAR_ACCESSIBLE_FROM_THREADS = (1 << 25),
		FCVAR_SERVER_CAN_EXECUTE = (1 << 28),
		FCVAR_SERVER_CANNOT_QUERY = (1 << 29),
		FCVAR_CLIENTCMD_CAN_EXECUTE = (1 << 30),
		FCVAR_MATERIAL_THREAD_MASK = (FCVAR_RELOAD_MATERIALS | FCVAR_RELOAD_TEXTURES | FCVAR_MATERIAL_SYSTEM_THREAD)
	};

	union cvar_value_t
	{
		bool i1;
		short i16;
		uint16_t u16;
		int i32;
		uint32_t u32;
		int64_t i64;
		uint64_t u64;
		float fl;
		double db;
		const char *sz;
		color clr;
		vector2d vec2;
		vector vec3;
		vector4d vec4;
		qangle ang;
	};

	// [14190 实锤 2026-10-12 v12 探针] ConVar 数据对象布局（cam_idealdist 指纹验证）：
	// name@+0x00 / next@+0x08（指向注册序相邻 convar，对象整体 0x60 步长）/
	// description@+0x20 / type@+0x28（7=float）/ registered@+0x2C / flags@+0x30
	//（cam_idealdist=0x88 与 convars.json 完全吻合）/ value@+0x58（getfloat=150.000 实锤）。
	struct convar
	{
		const char* name; // 0x0000
		convar* next; // 0x0008
		MEM_PAD(0x10); // 0x0010
		const char* description; // 0x0020
		uint32_t type; // 0x0028
		uint32_t registered; // 0x002C
		uint32_t flags; // 0x0030
		MEM_PAD(0x24); // 0x0034（min/max 等未解码字段）
		// @note: read-only, modify with caution
		cvar_value_t value; // 0x0058
	};

	// [14190] CCvar 对象本体布局未解码也无需解码：find 走虚表槽 11（FindVarIndex，
	// name→16 位索引），索引→ConVar 数据走 client.dll 注册表取值函数（cvar_from_index）。
	// 完整链路 v9~v12 探针实锤（2026-10-12）。全程 SEH，任何一环异常/指纹不过返回 null
	struct ccvar
	{
		convar* find(const char* name)
		{
			if (name == nullptr || name[0] == '\0')
				return nullptr;

			// 索引→数据函数在 client.dll（PEB 走查，纳秒级，无需缓存）
			const auto cl = reinterpret_cast<uintptr_t>(MEM::GetModuleBaseHandle(CLIENT_DLL));
			if (cl == 0)
				return nullptr;

			void** vft = nullptr;
			__try
			{
				vft = *reinterpret_cast<void***>(this);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}

			if (vft == nullptr)
				return nullptr;

			// ① 槽 11 FindVarIndex：(this, out*, name, 0) → out[0]=16 位 cvar 索引
			//（不存在=0xFFFFFFFF；idx==0 可能是合法 convar，不排除）
			unsigned char out[0x20];
			std::memset(out, 0xFF, sizeof(out));

			__try
			{
				reinterpret_cast<void* (__fastcall*)(void*, void*, const char*, int)>(vft[11])(this, out, name, 0);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}

			uint32_t idx = 0;
			std::memcpy(&idx, out, sizeof(idx));
			if (idx > 0xFFFF)
				return nullptr;

			// ② 索引→ConVar 数据：(out2*, idx, -1, 0) → out2 = {idx, ConVar 数据指针}
			unsigned char out2[0x30];
			std::memset(out2, 0, sizeof(out2));

			__try
			{
				reinterpret_cast<void* (__fastcall*)(void*, void*, int, int)>(cl + sdk::offsets::functions::client::cvar_from_index)(out2, reinterpret_cast<void*>(static_cast<uintptr_t>(idx)), -1, 0);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}

			convar* cvar = nullptr;
			std::memcpy(&cvar, out2 + sizeof(void*), sizeof(cvar));

			// ③ 合理性校验 + 名称复核：链路任何一环过期都宁可返回 null
			if (reinterpret_cast<uintptr_t>(cvar) <= 0x10000 || reinterpret_cast<uintptr_t>(cvar) >= 0x7FFFFFFFFFFFull)
				return nullptr;

			__try
			{
				if (cvar->name == nullptr || std::strcmp(cvar->name, name) != 0)
					cvar = nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				cvar = nullptr;
			}

			return cvar;
		}

		// 解锁隐藏/开发者 cvar（QoL：游戏控制台可见可用）。14190 的 CCvar 内部注册表
		// 布局未解码，无法定位链头 —— 从 sv_cheats（索引 399，注册序靠前）沿 next 链
		//（+0x08，注册序相邻 0x60 步长）尽力遍历。每节点严格校验，指纹不过立即停走：
		// 宁可少解锁，也不写坏内存
		void unlock()
		{
			const char* const anchors[] = { "sv_cheats", "cam_idealdist" };

			for (const char* anchor : anchors)
			{
				convar* cvar = find(anchor);
				if (cvar == nullptr)
					continue;

				for (int i = 0; i < 8192; ++i)
				{
					bool stop = false;
					convar* next = nullptr;

					__try
					{
						// convar 指纹：名称可读且可打印、类型合理
						if (cvar->name == nullptr)
							stop = true;
						else
						{
							const unsigned char first = static_cast<unsigned char>(cvar->name[0]);
							if (first < 0x20 || first > 0x7E || cvar->type > 16)
								stop = true;
							else
							{
								cvar->flags &= ~(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY);
								next = cvar->next;
							}
						}
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						stop = true;
					}

					if (stop || next == nullptr)
						break;

					// next 必须是用户态堆地址，防垃圾指针
					const auto n = reinterpret_cast<uintptr_t>(next);
					if (n <= 0x10000 || n >= 0x7FFFFFFFFFFFull)
						break;

					cvar = next;
				}
			}
		}
	};

} // namespace sdk

// no `static`: cached convar pointers dangle when CS2 rebuilds its cvar system
// during early startup (lobby injection) — re-find every call; callers null-check
#define var(name) auto name = sdk::Cvar->find(#name)
