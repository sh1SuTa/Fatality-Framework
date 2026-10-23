#pragma once
#include <cstddef>
#include <cstdint>
namespace sdk::offsets::snapshot {
inline constexpr std::uint32_t build_number = 14192;
// [14192] 版本号取自游戏 steam.inf：PatchVersion=1.41.9.2 / ClientVersion=2000935
// （VersionDate Oct 22 2026）。engine2+0x61CFEC 仍沿用 14191 的位置，等待运行时校验；
// 若启动报 build mismatch 而版本号已对，说明该 RVA 随构建漂移，需要重新定位。
// 83 3D ?? ?? ?? ?? 00 7F ?? 48 8D 0D C3 B7 41
// 获取83 3D后面?? ?? ?? ??，RIP+?? ?? ?? ??=engine_build_number
inline constexpr std::ptrdiff_t engine_build_number = 0x6ba02c;
}
