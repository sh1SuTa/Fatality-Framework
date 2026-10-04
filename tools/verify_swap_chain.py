"""Compile and test the actual scanner/resolver source without loading CS2 or a DLL.

Usage: python tools/verify_swap_chain.py --compiler path/to/g++.exe
Requires a Windows C++20 compiler. The fixture replaces only game/CRT dependencies.
"""
import argparse
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREAMBLE = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#define RENDERSYSTEM_DLL L"rendersystemdx11.dll"
#define MEM_PAD(SIZE) private: char padding[SIZE]; public:
struct IDXGISwapChain;
namespace CRT {
int CharToHexInt(char c) { return c <= '9' ? c - '0' : (c & ~32) - 'A' + 10; }
int StringCompareN(const char* a, const char* b, std::size_t n) { return std::strncmp(a, b, n); }
}
namespace MEM {
std::uint8_t* FindPatternEx(const std::uint8_t*, std::size_t, const std::uint8_t*, std::size_t, const char*);
std::vector<std::uint8_t*> FindPatternAllOccurrencesEx(const std::uint8_t*, std::size_t, const std::uint8_t*, std::size_t, const char*);
std::size_t PatternToBytes(const char*, std::uint8_t*, char*);
bool GetSectionInfo(const void*, const char*, std::uint8_t**, std::size_t*);
}
'''

TESTS = r'''
void executable_present() {}

int main(int argc, char** argv) {
    (void)&resolve_swap_chain; // compile the complete resolver; never load a game module
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<std::uint8_t> renderer((std::istreambuf_iterator<char>(input)), {});
        assert(renderer.size() == 0x4b4000);
        bool list = false;
        assert(find_swap_chain_slot(renderer.data() + 0x1000, 0x1e8c00,
            renderer.data(), renderer.size(), &list) == renderer.data() + 0x496058);
        assert(list);
        std::cout << "PASS: actual supplied PE .text resolves uniquely to linked-list slot RVA 0x496058\n";
    }
    std::uint8_t overlap[] = {1, 1, 1, 2};
    std::uint8_t pattern[] = {1, 1, 2};
    assert(MEM::FindPatternEx(overlap, 4, pattern, 3, nullptr) == overlap + 1);
    assert(MEM::FindPatternAllOccurrencesEx(overlap, 4, pattern, 3, nullptr).size() == 1);
    assert(MEM::FindPatternEx(pattern, 3, pattern, 3, nullptr) == pattern);
    assert(MEM::FindPatternEx(pattern, 3, pattern, 4, nullptr) == nullptr);
    assert(MEM::FindPatternEx(pattern, 3, pattern, 0, nullptr) == nullptr);
    assert(MEM::FindPatternEx(nullptr, 3, pattern, 1, nullptr) == nullptr);
    assert(MEM::FindPatternEx(pattern, 3, nullptr, 1, nullptr) == nullptr);
    assert(MEM::FindPatternAllOccurrencesEx(pattern, 3, pattern, 4, nullptr).empty());
    assert(MEM::FindPatternAllOccurrencesEx(pattern, 3, pattern, 0, nullptr).empty());
    assert(MEM::FindPatternAllOccurrencesEx(nullptr, 3, pattern, 1, nullptr).empty());
    assert(MEM::FindPatternAllOccurrencesEx(pattern, 3, nullptr, 1, nullptr).empty());
    std::uint8_t repeats[] = {1, 1, 1};
    std::uint8_t pair[] = {1, 1};
    assert(MEM::FindPatternAllOccurrencesEx(repeats, 3, pair, 2, nullptr).size() == 2);
    std::uint8_t singleton[] = {2};
    assert(MEM::FindPatternEx(pattern, 3, singleton, 1, nullptr) == pattern + 2);
    assert(MEM::FindPatternEx(pattern, 3, singleton, 1, "?") == pattern);

    // Independent brute-force oracle, including wildcards and overlapping matches.
    std::mt19937 rng(20261003);
    for (int trial = 0; trial < 10000; ++trial) {
        std::array<std::uint8_t, 40> data{}, needle{};
        std::array<char, 40> mask{};
        const std::size_t n = rng() % 33, m = rng() % 39;
        for (auto& c : data) c = rng() % 3;
        for (auto& c : needle) c = rng() % 3;
        for (auto& c : mask) c = rng() % 3 ? 'x' : '?';
        const char* selected_mask = trial % 2 ? mask.data() : nullptr;
        std::vector<std::uint8_t*> expected;
        if (m && m <= n)
            for (std::size_t off = 0; off + m <= n; ++off) {
                bool found = true;
                for (std::size_t j = 0; j < m; ++j)
                    if ((!selected_mask || selected_mask[j] != '?') && data[off+j] != needle[j]) found = false;
                if (found) expected.push_back(data.data() + off);
            }
        assert(MEM::FindPatternAllOccurrencesEx(data.data(), n, needle.data(), m, selected_mask) == expected);
        assert(MEM::FindPatternEx(data.data(), n, needle.data(), m, selected_mask) == (expected.empty() ? nullptr : expected.front()));
    }

    std::array<std::uint8_t, 512> image{};
    const char* old_pattern = "66 0F 7F 05 ? ? ? ? 66 0F 7F 0D ? ? ? ? 48 89 35";
    const char* alternate = "66 0F 7F 0D ? ? ? ? 48 8B F7 66 0F 7F 05 ? ? ? ?";
    const char* current = "48 89 2D ? ? ? ? 66 0F 7F 05 ? ? ? ? FF 15 ? ? ? ? 48 8D 0D";
    auto put = [&](const char* text, std::size_t off, int target) {
        std::uint8_t bytes[64]{}; char mask[64]{};
        auto count = MEM::PatternToBytes(text, bytes, mask);
        std::copy(bytes, bytes + count, image.begin() + off);
        const bool list = bytes[0] == 0x48;
        const std::int32_t disp = target - static_cast<int>(off) - (list ? 7 : 8);
        std::memcpy(image.data() + off + (list ? 3 : 4), &disp, 4);
    };
    auto slot = [&] { return find_swap_chain_slot(image.data(), image.size(), image.data(), image.size()); };
    put(old_pattern, 64, 32); // negative RIP displacement
    assert(slot() == image.data() + 32);
    image.fill(0); put(alternate, 64, 384);
    assert(slot() == image.data() + 384);
    put(old_pattern, 128, 384); // different code paths may reference the same slot
    assert(slot() == image.data() + 384);
    image.fill(0); put(old_pattern, 64, 32); put(old_pattern, 128, 32);
    assert(slot() == nullptr); // ambiguous signature
    image.fill(0); put(old_pattern, 64, 32); put(alternate, 128, 384);
    assert(slot() == nullptr); // conflicting slots
    image.fill(0); put(old_pattern, 64, -1); assert(slot() == nullptr);
    image.fill(0); put(old_pattern, 64, 508); assert(slot() == nullptr); // pointer would cross module end
    image.fill(0); assert(slot() == nullptr);
    image.fill(0); put(current, 64, 32);
    bool list = false;
    assert(find_swap_chain_slot(image.data(), image.size(), image.data(), image.size(), &list) == image.data() + 32);
    assert(list);
    image.fill(0); put(current, 64, 4); assert(slot() == nullptr); // metadata before module
    image.fill(0); put(current, 64, 504); assert(slot() == nullptr); // metadata after module

    // Real Windows reads, with synthetic game objects: no CS2 process is accessed.
    assert(read_ready_swap_chain(nullptr) == nullptr);
    assert(read_ready_swap_chain(reinterpret_cast<void*>(1)) == nullptr);
    ISwapChainDx11* chain_pointer = nullptr;
    ISwapChainDx11** chain_slot = &chain_pointer;
    assert(read_ready_swap_chain(&chain_slot) == nullptr);
    ISwapChainDx11 chain{};
    chain_pointer = &chain;
    assert(read_ready_swap_chain(&chain_slot) == nullptr);
    void* table[9]{};
    struct { void** vtable; } dxgi{table};
    chain.pDXGISwapChain = reinterpret_cast<IDXGISwapChain*>(&dxgi);
    assert(read_ready_swap_chain(&chain_slot) == nullptr); // null Present
    table[8] = &image; // readable data is not executable
    assert(read_ready_swap_chain(&chain_slot) == nullptr);
    table[8] = reinterpret_cast<void*>(&executable_present);
    assert(read_ready_swap_chain(&chain_slot) == &chain);
    struct entry_t { ISwapChainDx11* chain; std::int32_t previous; std::int32_t next; };
    static_assert(sizeof(entry_t) == 16);
    entry_t entries[2]{{reinterpret_cast<ISwapChainDx11*>(1), -1, -1}, {&chain, -1, -1}};
    struct list_t { std::int32_t count; std::uint32_t capacity; entry_t* storage; std::int32_t head; std::int32_t tail; };
    list_t active{2, 2, entries, 1, 1}; // entry 0 has been freed; the head is entry 1
    assert(read_ready_swap_chain(&active.storage, true) == &chain);
    active.head = -1; assert(read_ready_swap_chain(&active.storage, true) == nullptr);
    active.head = 2; assert(read_ready_swap_chain(&active.storage, true) == nullptr);
    active.head = 1; active.capacity = 1; assert(read_ready_swap_chain(&active.storage, true) == nullptr);
    active.capacity = 0x80000002; assert(read_ready_swap_chain(&active.storage, true) == &chain);
    chain_slot = reinterpret_cast<ISwapChainDx11**>(1);
    assert(read_ready_swap_chain(&chain_slot) == nullptr);
    std::cout << "PASS: scanner edge cases, 10000 randomized comparisons, RIP/ambiguity checks, active-list selection, Windows pointer validation\n";
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--renderer", type=Path, help="Optional supplied renderer snapshot, statically mapped for native tests")
    args = parser.parse_args()
    memory = (ROOT / "cs2_internal/src/memory/memory.cpp").read_text(encoding="utf-8")
    game = (ROOT / "cs2_internal/src/game/game.cpp").read_text(encoding="utf-8")
    header = (ROOT / "cs2_internal/src/game/game.h").read_text(encoding="utf-8")
    swap_type = function(header, "class ISwapChainDx11") + ";"
    functions = [function(memory, signature) for signature in (
        "std::uint8_t* MEM::FindPatternEx(",
        "std::vector<std::uint8_t*> MEM::FindPatternAllOccurrencesEx(",
        "std::size_t MEM::PatternToBytes(",
        "bool MEM::GetSectionInfo(",
    )]
    helpers = game[game.index("namespace\n{"):game.index("game_t::game_t(")]
    output = ROOT / "artifacts/swap-chain-tests"
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / "regression.cpp"
    exe = output / "regression.exe"
    cpp.write_text(PREAMBLE + swap_type + "\n" + "\n".join(functions) + "\n" + helpers + TESTS, encoding="utf-8")
    compiler = Path(args.compiler).resolve()
    subprocess.run([str(compiler), "-std=c++20", "-O2", "-Wall", "-Wextra", str(cpp), "-o", str(exe)], check=True)
    env = os.environ.copy()
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    command = [str(exe)]
    if args.renderer:
        from inspect_renderer import PE
        pe = PE(args.renderer)
        mapped = bytearray(pe.image_size)
        for section in pe.sections:
            data = pe.bytes(section)
            mapped[section["rva"]:section["rva"] + len(data)] = data
        image = output / "renderer-image.bin"
        image.write_bytes(mapped)
        command.append(str(image))
    subprocess.run(command, check=True, env=env)


if __name__ == "__main__":
    main()
