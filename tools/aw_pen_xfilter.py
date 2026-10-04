# -*- coding: utf-8 -*-
"""渗透函数交叉过滤：引用 0.1f 常量 + 访问 trace 特征偏移的函数"""
import struct
import sys

import capstone
from capstone.x86 import X86_OP_MEM, X86_REG_RIP, X86_REG_RSP, X86_REG_RBP

sys.path.insert(0, "tools")
from aw_surface import parse_pe, load_pdata, func_of, r2o, MD

data = open("reference/server.dll", "rb").read()
base, secs = parse_pe(data)
fs = load_pdata(data, secs)

cands = set()
for n, va, vs, ra, rs in secs:
    if n not in (".rdata", ".data"):
        continue
    blob = data[ra:ra + rs]
    start = 0
    while True:
        i = blob.find(struct.pack("<f", 0.1), start)
        if i < 0:
            break
        if i % 4 == 0:
            cands.add(va + i)
        start = i + 1

tsec = next(s for s in secs if s[0] == ".text")
tva, tsz, tra = tsec[1], tsec[2], tsec[3]
blob = data[tra:tra + tsz]

# 收集引用 0.1f 的函数
f01 = {}
for m in __import__("re").finditer(rb"[\x05\x0D\x15\x1D\x25\x2D\x35\x3D\x85\x8D\x95\x9D\xA5\xAD\xB5\xBD\xC5\xCD\xD5\xDD\xE5\xED\xF5\xFD]", blob):
    i = m.start()
    if i + 4 > len(blob):
        continue
    disp, = struct.unpack_from("<i", blob, i + 1)
    if tva + i + 5 + disp in cands:
        f = func_of(tva + i - 3, fs)
        if f:
            f01.setdefault(f, set()).add(tva + i)

print("0.1f 函数:", len(f01))

# 对每个函数反汇编，找 trace 特征偏移访问
HIT_OFFS = {0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C,  # surface / 记录区
            0xAC, 0xB0, 0xB8, 0xBA, 0xBB, 0x68}               # fraction/命中/句柄
out = []
for (fb, fe), refs in f01.items():
    o = r2o(fb, secs)
    if o is None:
        continue
    code = data[o:o + (fe - fb)]
    seen = set()
    try:
        for insn in MD.disasm(code, fb):
            for op in insn.operands:
                if op.type == X86_OP_MEM and op.mem.base not in (X86_REG_RIP, X86_REG_RSP, X86_REG_RBP):
                    d = op.mem.disp
                    if d in HIT_OFFS and insn.mnemonic.startswith(("mov", "cmp", "test")):
                        seen.add((d, insn.mnemonic.split()[0], op.mem.base))
    except Exception:
        pass
    if seen:
        out.append((fb, fe - fb, refs, sorted(seen)))

for fb, sz, refs, seen in sorted(out):
    print(f"func 0x{fb:X} size=0x{sz:X} refs01={[hex(r) for r in sorted(refs)]}")
    for s in seen[:14]:
        print("   ", s)
