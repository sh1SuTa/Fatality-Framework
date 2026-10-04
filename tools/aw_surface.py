# -*- coding: utf-8 -*-
"""
Autowall surface layout 分析工具箱 (client.dll 14188)
子命令:
  dis <dll> <rva> [print_from_rva] [print_to_rva]
      用 .pdata 定位包含函数，从函数起始整段反汇编（保证对齐），
      只打印 [print_from, print_to) 范围内指令。标注 call 目标/rip 相对数据目标。
  callers <dll> <rva>
      扫描 .text 中 E8 rel32 直接调用 + 48/4C 8D/8B rip 相对引用，回溯所属函数。
  colscan <dll>
      COL 自校验全量扫描 -> 全部 RTTI vtable -> slot56(=+0x1C0) 候选函数，统计 SSE 16B store。
  vtable <dll> <rva> [n]
      展开指定 RVA 处 vtable 的前 n 槽（默认 64）。
"""
import bisect
import re
import struct
import sys

import capstone
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP

SSE_ST = {"movups", "movaps", "movdqu", "movdqa",
          "vmovups", "vmovaps", "vmovdqu", "vmovdqa"}


def parse_pe(data):
    e = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e:e + 4] == b"PE\0\0"
    coff = e + 4
    nsec, = struct.unpack_from("<H", data, coff + 2)
    optsz, = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    base, = struct.unpack_from("<Q", data, opt + 24)
    secs = []
    sh = opt + optsz
    for i in range(nsec):
        o = sh + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode(errors="replace")
        vsz, va, rsz, ra = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, vsz, ra, rsz))
    return base, secs


def r2o(rva, secs):
    for n, va, vs, ra, rs in secs:
        if va <= rva < va + vs:
            d = rva - va
            return ra + d if d < rs else None
    return None


def r2sec(rva, secs):
    for n, va, vs, ra, rs in secs:
        if va <= rva < va + vs:
            return n
    return None


def load_pdata(data, secs):
    pd = next(s for s in secs if s[0] == ".pdata")
    off, end = pd[3], pd[3] + pd[4]
    fs = []
    while off + 12 <= end:
        b, e2, u = struct.unpack_from("<III", data, off)
        if b == 0 and e2 == 0:
            break
        fs.append((b, e2))
        off += 12
    fs.sort()
    return fs


def func_of(rva, fs):
    i = bisect.bisect_right(fs, (rva, 1 << 62)) - 1
    if i >= 0 and fs[i][0] <= rva < fs[i][1]:
        return fs[i]
    return None


MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True


def annotate(data, secs, fs, insn):
    notes = []
    for op in insn.operands:
        if op.type == X86_OP_IMM and insn.mnemonic in ("call", "jmp"):
            t = op.imm
            f = func_of(t, fs)
            notes.append(f"->0x{t:X}" + (f"(func 0x{f[0]:X})" if f else ""))
        elif op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            t = insn.address + insn.size + op.mem.disp
            s = r2sec(t, secs)
            o = r2o(t, secs)
            if o is not None and s in (".rdata", ".data"):
                q, = struct.unpack_from("<Q", data, o)
                notes.append(f"[0x{t:X} {s}]=0x{q:016X}")
            else:
                notes.append(f"[0x{t:X} {s}]")
    return "  ; ".join(notes)


def cmd_dis(argv):
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    fs = load_pdata(data, secs)
    rva = int(argv[1], 16)
    pfrom = int(argv[2], 16) if len(argv) > 2 else None
    pto = int(argv[3], 16) if len(argv) > 3 else None
    f = func_of(rva, fs)
    if not f:
        print(f"0x{rva:X} not in any .pdata function")
        return
    fb, fe = f
    print(f"; func 0x{fb:X}-0x{fe:X}  size=0x{fe - fb:X}")
    o = r2o(fb, secs)
    code = data[o:o + (fe - fb)]
    n = 0
    for insn in MD.disasm(code, fb):
        if pfrom is not None and insn.address < pfrom:
            continue
        if pto is not None and insn.address >= pto:
            break
        a = annotate(data, secs, fs, insn)
        print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}" + (f"   ; {a}" if a else ""))
        n += 1
    if n == 0:
        print("; (打印范围为空，检查 print_from/print_to 是否落在函数内)")


def cmd_callers(argv):
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    fs = load_pdata(data, secs)
    tgt = int(argv[1], 16)
    tsec = next(s for s in secs if s[0] == ".text")
    tva, tsz, tra = tsec[1], tsec[2], tsec[3]
    blob = data[tra:tra + tsz]
    refs = []
    for m in re.finditer(rb"\xE8", blob):
        i = m.start()
        if i + 5 > len(blob):
            break
        rel, = struct.unpack_from("<i", blob, i + 1)
        if tva + i + 5 + rel == tgt:
            refs.append(tva + i)
    for m in re.finditer(rb"[\x48\x4C][\x8D\x8B][\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", blob):
        i = m.start()
        disp, = struct.unpack_from("<i", blob, i + 3)
        if tva + i + 7 + disp == tgt:
            refs.append(tva + i)
    for h in sorted(set(refs)):
        f = func_of(h, fs)
        tail = f"func 0x{f[0]:X}-0x{f[1]:X}" if f else "??"
        print(f"  ref@0x{h:X} -> {tail}")
    print(f"  共 {len(set(refs))} 处引用")


def cmd_colscan(argv):
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    fs = load_pdata(data, secs)
    dsecs = [s for s in secs if s[0] in (".rdata", ".data")]
    tsec = next(s for s in secs if s[0] == ".text")
    tva, tvsz = tsec[1], tsec[2]

    # 1) COL 自校验扫描: [sig=1, mbr, cdoff, pTd, pCd, pSelf] 且 pSelf==自身RVA
    cols = {}  # col_rva -> td_rva
    n_sig = n_self = n_name = 0
    for n, va, vs, ra, rs in dsecs:
        blob = data[ra:ra + rs]
        for m in re.finditer(rb"\x01\x00\x00\x00", blob):
            o = m.start()
            if o + 24 > rs:
                continue
            n_sig += 1
            d = struct.unpack_from("<6I", blob, o)
            if d[5] != va + o:
                continue
            n_self += 1
            td = d[3]
            to = r2o(td + 0x10, secs)
            if to is None or data[to:to + 3] != b".?A":
                continue
            n_name += 1
            cols[va + o] = td
    print(f"sig=1命中={n_sig} pSelf自指={n_self} TD名字有效={n_name}")
    print(f"COL(自校验通过) = {len(cols)}")

    # 2) vtable 指针: 数据段 8 对齐 QWORD 指向某 COL
    vts = {}  # col_rva -> vt_rva
    for n, va, vs, ra, rs in dsecs:
        blob = data[ra:ra + rs]
        for o in range(0, rs - 8, 8):
            q, = struct.unpack_from("<Q", blob, o)
            r = q - base
            if 0 < r < 0x10000000 and r in cols:
                vts[r] = va + o + 8
    print(f"带 RTTI 的 vtable = {len(vts)}")

    # 3) 展开 vtable 取 slot56，按实现函数分组
    name_cache = {}

    def tdname(td):
        if td in name_cache:
            return name_cache[td]
        o = r2o(td + 0x10, secs)
        e = data.find(b"\0", o)
        nm = data[o:e].decode(errors="replace")
        name_cache[td] = nm
        return nm

    groups = {}  # fn56 -> set(类名)
    for col, td in cols.items():
        vt = vts.get(col)
        if vt is None:
            continue
        o = r2o(vt, secs)
        if o is None:
            continue
        slots = []
        for i in range(57):
            q, = struct.unpack_from("<Q", data, o + i * 8)
            fr = q - base
            if q == 0 or not (tva <= fr < tva + tvsz):
                break
            slots.append(fr)
        if len(slots) < 57:
            continue
        groups.setdefault(slots[56], set()).add(tdname(td))
    print(f"有效 slot56 实现数 = {len(groups)}\n")

    results = []
    for fn, names in groups.items():
        f = func_of(fn, fs)
        size = f[1] - f[0] if f else 0
        head = []
        if f:
            o = r2o(f[0], secs)
            code = data[o:o + min(size, 0x40)]
            for insn in MD.disasm(code, f[0]):
                if len(head) >= 5:
                    break
                head.append(f"    0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        results.append((size, fn, names, head))
    results.sort()
    print(f"=== 全部 {len(results)} 个 slot56 实现 ===")
    for size, fn, names, head in results:
        nl = sorted(names)
        print(f"fn=0x{fn:X} size={size} classes={len(nl)} e.g. {nl[:3]}")
        for h in head:
            print(h)


def cmd_vtable(argv):
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    vt = int(argv[1], 16)
    n = int(argv[2]) if len(argv) > 2 else 64
    o = r2o(vt, secs)
    for i in range(n):
        q, = struct.unpack_from("<Q", data, o + i * 8)
        fr = q - base
        if q == 0 or r2sec(fr, secs) != ".text":
            print(f"  [{i}] END")
            break
        print(f"  [{i}] 0x{fr:X}")


def cmd_probe(argv):
    """probe <dll> <col_rva>: 打印该处 24 字节并逐步验证 COL 条件"""
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    rva = int(argv[1], 16)
    o = r2o(rva, secs)
    print(f"rva=0x{rva:X} file_off=0x{o:X} sec={r2sec(rva, secs)}")
    d = struct.unpack_from("<6I", data, o)
    labels = ("sig", "off_mbr", "cd_off", "pTd", "pCd", "pSelf")
    for lab, v in zip(labels, d):
        print(f"  {lab} = 0x{v:X}")
    print(f"  pSelf==rva? {d[5] == rva}")
    to = r2o(d[3] + 0x10, secs)
    print(f"  pTd+0x10 off=0x{to:X} bytes={data[to:to + 12]!r}")


def cmd_funcs(argv):
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    fs = load_pdata(data, secs)
    a = int(argv[1], 16)
    b = int(argv[2], 16) if len(argv) > 2 else a + 0x800
    for fb, fe in fs:
        if a <= fb < b:
            print(f"  0x{fb:X}-0x{fe:X}  size=0x{fe - fb:X}")


def cmd_rawdis(argv):
    """rawdis <dll> <rva> [n_bytes]: 不经 .pdata 直接反汇编 N 字节（默认 0x40）"""
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    rva = int(argv[1], 16)
    n = int(argv[2], 16) if len(argv) > 2 else 0x40
    o = r2o(rva, secs)
    if o is None:
        print(f"0x{rva:X} 不可读")
        return
    for insn in MD.disasm(data[o:o + n], rva):
        a = annotate(data, secs, [], insn)
        print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}" + (f"   ; {a}" if a else ""))


def cmd_coltest(argv):
    """调试 colscan 第1步: 打印 name-check 失败样本的具体字节"""
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    dsecs = [s for s in secs if s[0] in (".rdata", ".data")]
    shown = 0
    n_self = 0
    for n, va, vs, ra, rs in dsecs:
        blob = data[ra:ra + rs]
        for m in re.finditer(rb"\x01\x00\x00\x00", blob):
            o = m.start()
            if o + 24 > rs:
                continue
            d = struct.unpack_from("<6I", blob, o)
            if d[5] != va + o:
                continue
            n_self += 1
            td = d[3]
            to = r2o(td + 0x10, secs)
            ok = to is not None and data[to:to + 4] == b".?A"
            if not ok and shown < 8:
                shown += 1
                if to is None:
                    print(f"col=0x{va + o:X} td=0x{td:X} td+0x10 -> r2o=None (越界或虚拟尾部)")
                else:
                    print(f"col=0x{va + o:X} td=0x{td:X} off=0x{to:X} bytes={data[to:to + 8]!r}")
    print(f"pSelf={n_self} shown={shown}")


def cmd_pat(argv):
    """pat <dll> <hex模式[,hex模式...]>: .text 字节模式搜索 + .pdata 回溯函数"""
    data = open(argv[0], "rb").read()
    base, secs = parse_pe(data)
    fs = load_pdata(data, secs)
    tsec = next(s for s in secs if s[0] == ".text")
    tva, tsz, tra = tsec[1], tsec[2], tsec[3]
    blob = data[tra:tra + tsz]
    pats = [bytes.fromhex(p.replace(" ", "")) for p in argv[1].split(",")]
    all_hits = {}
    for pat in pats:
        rx = re.compile(re.escape(pat))
        for m in rx.finditer(blob):
            rva = tva + m.start()
            f = func_of(rva, fs)
            all_hits.setdefault(f, []).append((rva, pat.hex()))
    for f, lst in sorted(all_hits.items(), key=lambda kv: (kv[0] or (0, 0))[0]):
        tag = f"func 0x{f[0]:X}-0x{f[1]:X}" if f else "??"
        for rva, ph in lst[:6]:
            print(f"  @0x{rva:X} [{ph}] -> {tag}")
        if len(lst) > 6:
            print(f"  ... 共{len(lst)}处 -> {tag}")
    print(f"共 {len(all_hits)} 个函数命中")


if __name__ == "__main__":
    cmds = {"dis": cmd_dis, "callers": cmd_callers, "colscan": cmd_colscan,
            "vtable": cmd_vtable, "funcs": cmd_funcs, "probe": cmd_probe,
            "coltest": cmd_coltest, "rawdis": cmd_rawdis, "pat": cmd_pat}
    cmds[sys.argv[1]](sys.argv[2:])
