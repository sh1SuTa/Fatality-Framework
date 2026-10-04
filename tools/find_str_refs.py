# -*- coding: utf-8 -*-
"""
字符串引用定位器：在 .text 中找引用给定 ASCII 字符串的指令（lea reg,[rip+disp]），
并用 .pdata 异常函数表把引用点回溯到所属函数起始 RVA。
用法:
  python find_str_refs.py <dll路径> <字符串1> [字符串2 ...]
"""
import bisect
import struct
import sys

import capstone


def parse_pe(data):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    num_sections, = struct.unpack_from("<H", data, coff + 2)
    opt_size, = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    image_base, = struct.unpack_from("<Q", data, opt + 24)
    sections = []
    sh = opt + opt_size
    for i in range(num_sections):
        off = sh + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode(errors="replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
        sections.append((name, vaddr, vsize, raddr, rsize))
    return image_base, sections


def parse_pdata(data, sections):
    """返回 (starts[], funcs[])：starts 排序后的函数起始 RVA，funcs 对应条目"""
    pd = next(s for s in sections if s[0] == ".pdata")
    begins, ents = [], []
    off = pd[3]
    end = off + pd[4]
    while off + 12 <= end:
        b, e, u = struct.unpack_from("<III", data, off)
        if b == 0 and e == 0:
            break
        begins.append(b)
        ents.append((b, e))
        off += 12
    return begins, ents


def main():
    path = sys.argv[1]
    needles = [s.encode() for s in sys.argv[2:]]
    data = open(path, "rb").read()
    image_base, sections = parse_pe(data)
    text = next(s for s in sections if s[0] == ".text")
    begins, ents = parse_pdata(data, sections)

    # 字符串定位
    targets = {}  # va -> label
    for nd in needles:
        pos = 0
        while True:
            idx = data.find(nd, pos)
            if idx < 0:
                break
            pos = idx + 1
            # 需要映射 file offset -> rva：简单起见用包含该 offset 的段
            for name, vaddr, vsize, raddr, rsize in sections:
                if raddr <= idx < raddr + rsize:
                    rva = vaddr + (idx - raddr)
                    targets[image_base + rva] = f"{nd.decode()}@0x{rva:X}"
                    break

    print(f"目标字符串 {len(targets)} 处，扫描 .text lea rip-relative ...")
    # lea r64, [rip+disp32] 编码: 48/4C 8D /r 其中 mod=00 rm=101 →
    # [48|4C] 8D [05|0D|15|1D|25|2D|35|3D] disp32，共 7 字节
    # 也可扩展 mov: 48/4C 8B 同款 ModRM 形态
    modrms = {0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D}
    refs = {}  # target_va -> [insn_rva...]
    tstart, tva, tsize, traddr, trsize = next(s for s in sections if s[0] == ".text")
    blob = data[traddr:traddr + trsize]
    n = len(blob)
    for i in range(n - 7):
        b0 = blob[i]
        if (b0 & 0xFE) != 0x48:  # 48 或 4C
            continue
        b1 = blob[i + 1]
        if b1 not in (0x8D, 0x8B):
            continue
        if blob[i + 2] not in modrms:
            continue
        disp, = struct.unpack_from("<i", blob, i + 3)
        tgt = tva + i + 7 + disp
        if tgt in targets:
            refs.setdefault(tgt, []).append(tva + i)

    for tgt, label in sorted(targets.items()):
        insns = refs.get(tgt, [])
        print(f"\n[{label}]  被引用 {len(insns)} 次:")
        for irva in insns:
            i = bisect.bisect_right(begins, irva) - 1
            if i >= 0:
                fb, fe = ents[i]
                print(f"    ins@0x{irva:X}  -> func 0x{fb:X}-0x{fe:X} (size {fe - fb})")
            else:
                print(f"    ins@0x{irva:X}  -> 未在 .pdata 中")


if __name__ == "__main__":
    main()
