# -*- coding: utf-8 -*-
"""
MSVC x64 RTTI 扫描器（纯 stdlib）：
  type_info 名字字符串 -> type descriptor -> Complete Object Locator -> vtable
用法:
  python rtti_scan.py <dll路径> <名字过滤正则,如 "Trace|Ray">
  python rtti_scan.py <dll路径> <正则> --slots <类名>   # 展开某类的完整 vtable
"""
import re
import struct
import sys


def parse_pe(data):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0", "not a PE"
    coff = e_lfanew + 4
    num_sections, = struct.unpack_from("<H", data, coff + 2)
    opt_size, = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    magic, = struct.unpack_from("<H", data, opt)
    assert magic == 0x20B, "only PE32+"
    image_base, = struct.unpack_from("<Q", data, opt + 24)
    sections = []
    sh = opt + opt_size
    for i in range(num_sections):
        off = sh + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode(errors="replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
        sections.append((name, vaddr, vsize, raddr, rsize))
    return image_base, sections


def rva_to_off(rva, sections):
    for name, vaddr, vsize, raddr, rsize in sections:
        if vaddr <= rva < vaddr + vsize:
            d = rva - vaddr
            if d < rsize:
                return raddr + d
            return None
    return None


def off_to_rva(off, sections):
    for name, vaddr, vsize, raddr, rsize in sections:
        if raddr <= off < raddr + rsize:
            return vaddr + (off - raddr)
    return None


def rva_to_section(rva, sections):
    for name, vaddr, vsize, raddr, rsize in sections:
        if vaddr <= rva < vaddr + vsize:
            return name
    return None


def main():
    path, pattern = sys.argv[1], sys.argv[2]
    expand = None
    if len(sys.argv) >= 5 and sys.argv[3] == "--slots":
        expand = sys.argv[4]
    rx = re.compile(pattern)
    data = open(path, "rb").read()
    image_base, sections = parse_pe(data)

    # 1) 收集所有 type_info 名字：".?A?" 开头，读到 NUL
    #    注意：engine2.dll 的 type_info 名字在 .data 段，不只 .rdata
    type_descs = {}  # td_rva -> mangled name
    data_secs = [s for s in sections if s[0] in (".rdata", ".data")]
    for sec_name, vaddr, vsize, raddr, rsize in data_secs:
        blob = data[raddr:raddr + rsize]
        for m in re.finditer(rb"\.\?A[VUW][A-Za-z0-9_$?]*", blob):
            off = raddr + m.start()
            end = data.find(b"\0", off)
            name = data[off:end].decode(errors="replace")
            td_rva = off_to_rva(off, sections) - 0x10  # type_info: +0x0 vftable +0x8 spare +0x10 name
            type_descs[td_rva] = name

    hits = {rva: n for rva, n in type_descs.items() if rx.search(n)}
    print(f"type_info 总数={len(type_descs)}  命中={len(hits)}")

    # 2) 在数据段中找 4 字节 RVA 引用 -> COL（x64 signature=1, pSelf 自指校验）
    results = []  # (td_rva, name, col_rva, vtable_rva)
    for td_rva, name in sorted(hits.items()):
        td_le = struct.pack("<I", td_rva)
        for sec_name, vaddr, vsize, raddr, rsize in data_secs:
            blob = data[raddr:raddr + rsize]
            pos = 0
            while True:
                idx = blob.find(td_le, pos)
                if idx < 0:
                    break
                pos = idx + 1
                col_rva = vaddr + idx - 0x0C
                if col_rva < vaddr:
                    continue
                co = rva_to_off(col_rva, sections)
                if co is None:
                    continue
                sig, off_mbr, cd_off, p_td, p_cd, p_self = struct.unpack_from("<IIIIII", data, co)
                if sig != 1 or p_td != td_rva or p_self != col_rva:
                    continue
                # 3) 找 vtable：8 字节 VA 指向 COL（vtable[-1]），vtable 从其后开始
                va_le = struct.pack("<Q", image_base + col_rva)
                for sec_name2, vaddr2, vsize2, raddr2, rsize2 in data_secs:
                    b2 = data[raddr2:raddr2 + rsize2]
                    j = b2.find(va_le)
                    if j >= 0:
                        results.append((td_rva, name, col_rva, vaddr2 + j + 8))
                        break
    for td_rva, name, col_rva, vt_rva in results:
        print(f"  {name}\n    TD=0x{td_rva:X}  COL=0x{col_rva:X}  vtable=0x{vt_rva:X}")

    # 4) 可选：展开某类 vtable
    if expand:
        for td_rva, name, col_rva, vt_rva in results:
            if expand in name:
                print(f"\n=== vtable of {name} (RVA 0x{vt_rva:X}) ===")
                off = rva_to_off(vt_rva, sections)
                i = 0
                while True:
                    fn_va, = struct.unpack_from("<Q", data, off + i * 8)
                    if fn_va == 0:
                        break
                    fn_rva = fn_va - image_base
                    sec = rva_to_section(fn_rva, sections)
                    if sec != ".text":
                        break
                    print(f"  [{i}] 0x{fn_rva:X}")
                    i += 1
                print(f"  共 {i} 槽")


if __name__ == "__main__":
    main()
