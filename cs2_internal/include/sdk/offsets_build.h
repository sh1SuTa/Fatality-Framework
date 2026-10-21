#pragma once
#include <cstddef>
#include <cstdint>
namespace sdk::offsets::snapshot {
inline constexpr std::uint32_t build_number = 14191;
// [14191] engine2 steam.inf 解析函数（0x2476FB..0x247A75）ServerVersion 分支实锤：
// 版本串去点（"1.41.9.1"→14191）→ atoi → 写 [engine2+0x61CFEC]；旧 0x61CFE8 已 +4 漂移
inline constexpr std::ptrdiff_t engine_build_number = 0x61cfec;
}
