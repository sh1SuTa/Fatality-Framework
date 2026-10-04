# -*- coding: utf-8 -*-
"""窗口式裸 vtable 扫描: 跨度>=173槽、窗口内有效项>=150, 检查 slot56/143/171/172"""
import bisect
import struct
import sys

sys.path.insert(0, r"tools")
from aw_surface import func_of, load_pdata, parse_pe, r2o

data = open(r"reference\client.dll", "rb").read()
base, secs = parse_pe(data)
fs = load_pdata(data, secs)
starts = set(f[0] for f in fs)

pos_val = {}  # rva -> valid
for name, va, vs, ra, rs in secs:
    if name not in (".rdata", ".data"):
        continue
    blob = data[ra:ra + rs]
    n = rs // 8
    vals = struct.unpack_from("<%dQ" % n, blob, 0)
    for k in range(n):
        r = vals[k] - base
        pos_val[va + k * 8] = 0 < r < 0x2000000 and r in starts

ps = sorted(pos_val)
pref = [0]
for p in ps:
    pref.append(pref[-1] + (1 if pos_val[p] else 0))

SPAN = 173 * 8
out = []
i = 0
while i < len(ps):
    p = ps[i]
    if not pos_val[p]:
        i += 1
        continue
    j = bisect.bisect_right(ps, p + SPAN - 8) - 1
    span_cnt = j - i + 1
    valid = pref[j + 1] - pref[i]
    if span_cnt >= 173 and valid >= 150 and pos_val[p] and pos_val[ps[j]]:
        out.append((p, span_cnt, valid))
        i = j + 1
        continue
    i += 1

print("窗口候选 =", len(out))
for vt, span, valid in out:
    o = r2o(vt, secs)
    slots = {}
    for name, off in (("s56", 0x1C0), ("s143", 0x478), ("s171", 0x558), ("s172", 0x560)):
        q, = struct.unpack_from("<Q", data, o + off)
        fr = q - base if base < q < base + 0x2000000 else 0
        sz = 0
        if fr in starts:
            f = func_of(fr, fs)
            sz = f[1] - f[0]
        slots[name] = (fr, sz)
    print(f"vt=0x{vt:X} span={span} valid={valid} " +
          " ".join(f"{k}=0x{v[0]:X}({v[1]})" for k, v in slots.items()))
