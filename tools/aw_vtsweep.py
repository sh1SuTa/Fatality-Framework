# -*- coding: utf-8 -*-
"""扫描无 RTTI 裸 vtable（连续 >=160 个 qword 指向已知函数起始），检查 slot56/slot143 形态"""
import struct
import sys

sys.path.insert(0, r"tools")
import capstone

from aw_surface import func_of, load_pdata, parse_pe, r2o

data = open(r"reference\client.dll", "rb").read()
base, secs = parse_pe(data)
fs = load_pdata(data, secs)
starts = set(f[0] for f in fs)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

cands = []
for name, va, vs, ra, rs in secs:
    if name not in (".rdata", ".data"):
        continue
    blob = data[ra:ra + rs]
    n = rs // 8
    vals = struct.unpack_from("<%dQ" % n, blob, 0)
    run = j = 0
    for k in range(n):
        r = vals[k] - base
        ok = 0 < r < 0x2000000 and r in starts
        if ok:
            if run == 0:
                j = k
            run += 1
        else:
            if run >= 160:
                cands.append((va + j * 8, run))
            run = 0
    if run >= 160:
        cands.append((va + j * 8, run))

print("裸 vtable 候选(>=160槽) =", len(cands))
for vt, n in cands:
    o = r2o(vt, secs)
    s56, = struct.unpack_from("<Q", data, o + 0x1C0)
    s143, = struct.unpack_from("<Q", data, o + 0x478)
    f56 = s56 - base if base < s56 < base + 0x2000000 else 0
    f143 = s143 - base if base < s143 < base + 0x2000000 else 0
    sz56 = 0
    if f56 in starts:
        fn = func_of(f56, fs)
        sz56 = fn[1] - fn[0]
    # getter 候选: slot56 是已知函数且短(<=0x80)
    tag = " <== GETTER?" if 0 < sz56 <= 0x80 else ""
    print(f"vt=0x{vt:X} slots={n} slot56=0x{f56:X}(sz={sz56}) slot143=0x{f143:X}{tag}")
