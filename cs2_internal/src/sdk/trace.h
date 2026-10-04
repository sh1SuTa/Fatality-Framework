#pragma once

// CGameTrace / CGameSurfaceProperties —— build 14188 静态分析实锤布局
// （server.dll 对称分析 2026-10-05，client/server 展开器逐指令同构：
//   server 0xBE77CA ≡ client 0x9D31A0 路径，结论可直接用于 client）
// 用途：autowall 只读这些偏移，零游戏函数调用。

#include <cstdint>

namespace sdk
{
	// 32B surfaceprop 数据表元素。
	// 证据：server KV 解析链 0xBE20C0（表基址 [List this+0x28]，元素步长 32）+
	// 尾段 chunk 0xBE2384（gamematerial）+ 0xBE23B1（两个 penetration modifier，
	// 字段名字符串 @.rdata 0x19466B8 / 0x19466E0）。
	// token = KV 读取键（string token）。
	struct cgame_surface_properties_t
	{
		float maxspeed;                              // +0x00 token 0xCD6C188F
		float jumpfactor;                            // +0x04 token 0xDECF595E
		float bullet_penetration_distance_modifier;  // +0x08 token 0xA3E47957（0xBE23B1 写入实锤）
		float bullet_penetration_damage_modifier;    // +0x0C token 0x2BB70753（0xBE23B1 写入实锤）
		std::uint32_t legacy_material;               // +0x10 token 0x5644F380（hash，seed 0x31415926）
		std::uint16_t game_material;                 // +0x14 token 0x11987413
		std::uint16_t sound_data;                    // +0x16 token 0x5ADC5A9F（0xFFFF=无效）
		std::uint8_t climbable;                      // +0x18 token 0x97994227
		std::uint8_t flags[6];                       // +0x19~0x1E 六 bool（reduceswimming/bulletsreflect…名字@0x19465F0~0x1946650）
		std::uint8_t pad;                            // +0x1F
	};

	static_assert(sizeof(cgame_surface_properties_t) == 0x20);

	// 0xC0 trace 出参。
	// 证据：client 0x9D31A0（0x48 记录→CGameTrace 展开器）命中路径 + 总大小
	// 12×16B 整块拷贝（0xA1B0E1 起）；句柄解析哨兵 0x8000/-1/-2。
	// surface = objA vtable slot56（+0x1C0）getter 返回的 32B 原样拷贝。
	struct cgame_trace_t
	{
		std::uint32_t unknown_00;            // +0x00（0x48 记录 +0x28 透传，语义未定）
		std::uint32_t unknown_04;
		void* hit_entity;                    // +0x08 解析后的实体指针
		std::uint64_t unknown_10;
		void* obj_a;                         // +0x18 对象A 裸指针（非 CHandle）
		void* obj_b;                         // +0x20 对象B 裸指针
		std::uint64_t unknown_28;
		cgame_surface_properties_t surface;  // +0x30..0x4F 32B 内联拷贝
		std::uint8_t plane[0x18];            // +0x50..0x67（vec3 法线 + dist 推测，逐字段待 dump 验证）
		std::uint32_t entity_handle;         // +0x68 CEntityHandle 原始 dword（0x8000=无效）
		std::uint8_t unknown_6C[0x0C];
		float endpos[3];                     // +0x78..0x83
		float dir_84[3];                     // +0x84..0x8F vec3（语义待定）
		float normal_90[3];                  // +0x90..0x9B vec3（疑似命中法线）
		std::uint8_t unknown_9C[0x0C];
		float unknown_A8;
		float fraction;                      // +0xAC（0..1）
		float unknown_B0;
		std::uint8_t unknown_B4[6];
		std::uint8_t flag_BA;                // +0xBA bool（源 ray+0x28）
		std::uint8_t hit;                    // +0xBB 命中 byte
		std::uint8_t tail[4];                // +0xBC..0xBF
	};

	static_assert(sizeof(cgame_trace_t) == 0xC0);
} // namespace sdk
