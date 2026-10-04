# DX11 交换链查找修正（2026-10-03）

已针对用户提供的 `rendersystemdx11.dll` 完成静态定位并修改源码。旧特征码和最初参考上游的备用特征码在该文件中均为 **0 次匹配**；新特征码唯一匹配，且解析到实际交换链列表的存储指针。**已通过离线检查，但尚未编译整个框架或在报错机器上加载验证。**

## 依据

- 文件：仓库根目录的 `rendersystemdx11.dll`，4,545,688 字节。
- SHA-256：`321af39487adad1b0f0cfc7e0ad519c5b07937a40e16593fdfd12e1f67da8a56`。
- PE：x64 / PE32+，映像大小 `0x4B4000`，首选基址 `0x180000000`。
- PE 时间戳：`2026-09-30 18:57:16 UTC`，北京时间 `2026-10-01 02:57:16`。这是链接时间戳，不能据此确定 CS2 游戏 build；文件没有版本资源。

新模式：

```text
48 89 2D ? ? ? ? 66 0F 7F 05 ? ? ? ? FF 15 ? ? ? ? 48 8D 0D
```

唯一匹配位于 `.text` 的 RVA **`0x2BA56`**，第一条指令是 `mov qword ptr [rip+disp32], rbp`。位移位于 `+3`，指令长 `7` 字节，解析目标为 `.data` 中的 RVA **`0x496058`**。不能沿用旧 SIMD 指令的 `+4 / +8` 解析参数。

反汇编核验：

| RVA | 观察 | 含义 |
| --- | --- | --- |
| `0x2BA56` | 初始化 `0x496058`；下一条 SIMD 写入 `0x496060`。 | 列表存储指针和头/尾等元数据。 |
| `0x3F320` | 构造交换链对象；成功后使用设备 `+0x6D8` 的存储指针，将对象写入 `index * 16`。 | 列表项包含对象指针，项大小 16 字节。 |
| `0x338A0` | 从设备 `+0x6E0` 读取活动头索引，使用 `+0x6D8` 的数组，按记录 `+0xC` 跟随下一项。 | 应按活动头节点取对象，不能假定元素 0 仍有效。 |
| `0x3E613` | `lea r9, [rbx+0x170]`，随后交给 DXGI 工厂创建交换链。 | 实际 DXGI 对象指针输出位置仍是包装对象 `+0x170`。 |
| `0x3F25E` / `0x3F281` | 从对象 `+0x170` 取指针，调用 ResizeTarget / ResizeBuffers；错误字符串确认用途。 | 独立支持 `0x170` 的成员含义。 |

RTTI 显示静态设备对象 RVA `0x495980` 使用 `CRenderDeviceDx11` 的虚表，且 `0x495980 + 0x6D8 = 0x496058`；全局 RVA `0x433580` 的文件数据也指向这个静态设备。以上通过读取 PE 数据、RTTI 和 objdump 反汇编完成，没有加载或执行用户提供的 DLL。

## 修改的文件

- `cs2_internal/src/game/game.cpp`：新增该文件核验过的模式，使用各模式自己的有符号 RIP 参数；检查唯一性、目标一致性及模块范围。当前布局读取列表容量/计数/头索引，取活动头节点的对象，再检查 DXGI 指针和可执行的 Present 地址。模块加载与对象创建各最多等待 5 秒。历史模式仍保留。
- `cs2_internal/src/memory/memory.cpp`：修复两个扫描函数漏掉最后一个候选位置、跳过重叠匹配，以及模式过长导致无符号下溢的问题。
- `cs2_internal/src/hooks/steam.cpp`：Present 回调将实际传入的 `chain` 转发给原函数，避免多个交换链调用同一函数时误用初始化记录的对象。
- `tools/verify_swap_chain.py`：直接提取生产源码编译，测试扫描、RIP 地址、活动列表及 Windows 指针检查。支持把该 PE 的节数据静态映射到普通内存中，测试生产解析函数的实际匹配结果。
- `tools/inspect_renderer.py`：读取 PE 和生产源码的模式定义，生成匹配数量及目标地址报告。

修改前的源码（包括用户已有修改）保存在 `original-swap-chain/2026-10-03/` 下的 `game.cpp`、`memory.cpp` 和 `steam.cpp`。

两个历史模式参考原代码和 README 推荐的 [asphyxia interfaces.cpp](https://github.com/maecry/asphyxia-cs2/blob/master/cstrike/core/interfaces.cpp)；它们在本次提供的二进制中不匹配。新模式及列表布局来自实际提供的文件。

## 验证

使用本机 CLion 已带的 GCC 15.2.0 / MinGW，独立 C++20 回归检查通过：

- 生产解析函数在实际 PE `.text` 数据中唯一解析到 `0x496058`，并选中链表布局。
- 扫描边界、重叠匹配及 10000 组随机数据与独立穷举结果比较。
- 三种候选模式、正负 RIP 位移、重复匹配、冲突目标和模块越界拒绝。
- 活动头节点不为 0、元素 0 已失效、空列表、容量不足和越界索引检查。
- 实际 Windows 当前进程读内存：无效指针、空指针和非可执行 Present 地址拒绝。
- 原偏移检查仍通过：7 项测试和 13441 个常量核对；`git diff --check` 通过。

```powershell
$taskPython = 'C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $taskPython .\tools\inspect_renderer.py .\rendersystemdx11.dll --output .\artifacts\renderer-analysis.json
& $taskPython .\tools\verify_swap_chain.py --compiler 'D:\CLion 2025.2.1\bin\mingw\bin\g++.exe' --renderer .\rendersystemdx11.dll
```

报告保存在 `artifacts/renderer-analysis.json`。测试产物在 `artifacts/swap-chain-tests/`；它们是独立回归程序，不是可加载到游戏的框架 DLL。

## 仍需游戏内验证

本机没有可用的 MSVC 工程工具链，未完整编译该框架。生产解析函数的独立编译和测试不能替代完整工程编译，也不能确认所有旧接口、Hook、手工结构和功能均适配。

报错机器需用自己的构建环境重编译框架并重新运行。交换链阶段成功时输出 `Fatality: DX11 swap-chain resolved.`。其他失败会区分模块未加载、无匹配、匹配歧义、RIP 越界和列表/对象未就绪。

如果仍失败，提供完整调试输出即可进一步定位。原有 **build 14188** 检查仍保留；渲染模块不包含核验该游戏 build 和客户端/引擎偏移所需的全部数据。若出现 build 不匹配，需要同版本 `engine2.dll`、`client.dll` 或可靠的对应偏移数据，不能直接关闭检查。
