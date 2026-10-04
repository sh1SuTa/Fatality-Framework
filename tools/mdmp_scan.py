# minidump quick scanner: exception + faulting-thread stack module scan
import struct, sys

path = sys.argv[1]
data = open(path, 'rb').read()
assert data[:4] == b'MDMP', 'not a minidump'
nstreams, dirrva = struct.unpack_from('<II', data, 8)

streams = {}
for i in range(nstreams):
    st, sz, rva = struct.unpack_from('<III', data, dirrva + i * 12)
    streams.setdefault(st, (sz, rva))

mods = []  # (base, size, name)
if 4 in streams:
    sz, rva = streams[4]
    n, = struct.unpack_from('<I', data, rva)
    for i in range(n):
        e = rva + 4 + i * 108
        base, isz = struct.unpack_from('<QI', data, e)
        name_rva, = struct.unpack_from('<I', data, e + 20)
        ln, = struct.unpack_from('<I', data, name_rva)
        name = data[name_rva + 4:name_rva + 4 + ln].decode('utf-16-le', 'replace')
        mods.append((base, isz, name))

def mod_of(addr):
    for base, isz, name in mods:
        if base <= addr < base + isz:
            return base, isz, name
    return None

exc = None
if 6 in streams:
    sz, rva = streams[6]
    tid, = struct.unpack_from('<I', data, rva)
    code, flags = struct.unpack_from('<II', data, rva + 8)
    exaddr, = struct.unpack_from('<Q', data, rva + 16)
    nparams, = struct.unpack_from('<Q', data, rva + 24)
    params = struct.unpack_from('<15Q', data, rva + 40)
    ctx_sz, ctx_rva = struct.unpack_from('<II', data, rva + 160)
    rip, rsp = struct.unpack_from('<QQ', data, ctx_rva + 0xF8)
    exc = (tid, code, exaddr, nparams, params, rip, rsp)

print('== modules (client/internal only)')
for base, isz, name in mods:
    low = name.lower()
    if 'client.dll' in low or 'cs2_internal' in low or 'server.dll' in low:
        print('  %016X +%05X  %s' % (base, isz, name))

if exc:
    tid, code, exaddr, nparams, params, rip, rsp = exc
    print('== exception')
    print('  tid=%08X code=%08X addr=%016X nparams=%d' % (tid, code, exaddr, nparams))
    for i, p in enumerate(params[:nparams]):
        print('    param%d = %016X' % (i, p))
    m = mod_of(exaddr)
    print('  addr-> %s' % ('%s+%X' % (m[2], exaddr - m[0]) if m else 'unknown'))
    m = mod_of(rip)
    print('  rip=%016X -> %s' % (rip, '%s+%X' % (m[2], rip - m[0]) if m else 'unknown'))

# faulting thread stack memory: find thread by tid (MINIDUMP_THREAD: tid0 susp4 prio4 prio4 teb8 stack{start8 size4 rva4} ctx{size4 rva4} = 48B)
stack = None
if 3 in streams:
    sz, rva = streams[3]
    n, = struct.unpack_from('<I', data, rva)
    for i in range(n):
        e = rva + 4 + i * 48
        th, = struct.unpack_from('<I', data, e)
        if exc and th == exc[0]:
            st_start, st_sz, st_rva = struct.unpack_from('<QII', data, e + 24)
            stack = (st_start, st_sz, st_rva)
if not stack and exc:
    sz, rva = streams[3]
    n, = struct.unpack_from('<I', data, rva)
    for i in range(n):
        e = rva + 4 + i * 48
        st_start, st_sz, st_rva = struct.unpack_from('<QII', data, e + 24)
        if st_start <= exc[5] < st_start + st_sz:
            stack = (st_start, st_sz, st_rva)
            break

if stack:
    st_start, st_sz, st_rva = stack
    blob = data[st_rva:st_rva + st_sz]
    print('== faulting thread stack scan (%d bytes @%016X): module returns' % (st_sz, st_start))
    seen = []
    for off in range(0, len(blob) - 8, 8):
        v, = struct.unpack_from('<Q', blob, off)
        m = mod_of(v)
        if m:
            low = m[2].lower()
            tag = None
            if 'client.dll' in low and 'csgo' in low:
                tag = 'client'
            elif 'cs2_internal' in low:
                tag = 'OURDLL'
            elif 'server.dll' in low and 'csgo' in low:
                tag = 'server'
            elif 'panorama' in low:
                tag = 'pano'
            if tag:
                seen.append((st_start + off, v, tag, v - m[0]))
    for saddr, v, tag, rva in seen[:48]:
        print('  [stk %016X] %s+%X' % (saddr, tag, rva))
    print('  total=%d' % len(seen))
else:
    print('no stack found for faulting thread')
