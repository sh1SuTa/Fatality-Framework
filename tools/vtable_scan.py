# 静态解析 PE 的 RTTI：按类名定位 vftable，导出各槽位函数 RVA + 机器码
# 用法: python tools/vtable_scan.py reference\client.dll CCSGOInput [槽位数]
import struct
import sys


def parse_pe(data):
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b'PE\0\0', 'not a PE file'
    coff = e_lfanew + 4
    machine, nsec, _, _, _, optsize, _ = struct.unpack_from('<HHIIIHH', data, coff)
    assert machine == 0x8664, 'x64 only'
    opt = coff + 20
    magic = struct.unpack_from('<H', data, opt)[0]
    assert magic == 0x20B, 'PE32+ only'
    image_base = struct.unpack_from('<Q', data, opt + 24)[0]
    sec_tab = opt + optsize
    sections = []
    for i in range(nsec):
        o = sec_tab + i * 40
        name = data[o:o + 8].rstrip(b'\0').decode(errors='replace')
        vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, o + 8)
        sections.append((name, vaddr, vsize, raddr, rsize))
    return image_base, sections


def rva_to_off(rva, sections):
    for name, va, vs, ra, rs in sections:
        if va <= rva < va + max(vs, rs):
            if rva - va < rs:  # 只能读有原始数据的部分
                return ra + (rva - va)
    return None


def section_of(rva, sections):
    for name, va, vs, ra, rs in sections:
        if va <= rva < va + vs:
            return name
    return '?'


def find_type_descriptors(data, sections, needle):
    # type_info x64: { void* pVFTable; void* spare; char name[] }，name 从 +0x10 开始
    hits = []
    pat = b'.?AV' + needle.encode()
    for name, va, vs, ra, rs in sections:
        if rs == 0:
            continue
        raw = data[ra:ra + rs]
        start = 0
        while True:
            i = raw.find(pat, start)
            if i < 0:
                break
            hits.append(va + i - 0x10)  # TypeDescriptor RVA
            start = i + 1
    return hits


def find_cols(data, sections, td_rva):
    # x64 COL: { DWORD sig; DWORD off; DWORD cdOff; DWORD tdRVA; DWORD cdRVA; DWORD selfRVA }
    cols = []
    for name, va, vs, ra, rs in sections:
        if rs == 0 or name not in ('.rdata', '.data'):
            continue
        raw = data[ra:ra + rs]
        start = 0
        while True:
            i = raw.find(struct.pack('<I', td_rva), start)
            if i < 0:
                break
            start = i + 1
            if i < 12:
                continue
            col_off = i - 12
            if col_off + 24 > len(raw):
                continue
            sig, off, cdoff, td2, cd, self_rva = struct.unpack_from('<IIIIII', raw, col_off)
            if sig == 1 and td2 == td_rva and self_rva == va + col_off:
                cols.append((va + col_off, off))  # (COL RVA, this-offset)
    return cols


def find_vftables(data, sections, col_rva, image_base):
    # vftable 的 meta 槽（函数数组前 N 字节）指向 COL；本 build 用 8 字节绝对 VA
    # （加载后由重定位修正），也兼容 4 字节 image-relative RVA 的常规布局。
    text_range = None
    for name, va, vs, ra, rs in sections:
        if name == '.text':
            text_range = (va + 0x1000, va + vs)
    vfts = []
    for name, va, vs, ra, rs in sections:
        if rs == 0 or name not in ('.rdata', '.data'):
            continue
        raw = data[ra:ra + rs]

        def valid_vft(start_rva):
            if text_range is None:
                return True
            off = rva_to_off(start_rva, sections)
            if off is None:
                return False
            fn = struct.unpack_from('<Q', data, off)[0] - image_base
            return text_range[0] <= fn < text_range[1]

        # 8 字节绝对 VA（本 build 布局）
        start = 0
        while True:
            i = raw.find(struct.pack('<Q', image_base + col_rva), start)
            if i < 0:
                break
            start = i + 1
            vft_rva = va + i + 8
            if valid_vft(vft_rva):
                vfts.append(vft_rva)
        # 4 字节 RVA（常规 MSVC 布局）
        start = 0
        while True:
            i = raw.find(struct.pack('<I', col_rva), start)
            if i < 0:
                break
            start = i + 1
            vft_rva = va + i + 4
            if valid_vft(vft_rva):
                vfts.append(vft_rva)
    return vfts


def classify(b):
    # 常见微型函数形态
    if b[:3] == b'\x0f\xb6\x41' and b[4:5] == b'\xc3':
        return f'movzx eax, byte [rcx+0x{b[3]:x}]; ret'
    if b[:3] == b'\x0f\xb6\x81' and b[7:8] == b'\xc3':
        return f'movzx eax, byte [rcx+0x{struct.unpack_from("<I", b, 3)[0]:x}]; ret'
    if b[:2] == b'\x8a\x41' and b[3:4] == b'\xc3':
        return f'mov al, [rcx+0x{b[2]:x}]; ret'
    if b[:3] == b'\x8b\x41' and b[4:5] == b'\xc3':
        return f'mov eax, [rcx+0x{b[3]:x}]; ret'
    if b[:2] == b'\x32\xc0' and b[3:4] == b'\xc3':
        return 'xor al, al; ret (return false)'
    if b[:5] in (b'\xb8\x00\x00\x00\x00', b'\xb8\x01\x00\x00\x00') and b[5:6] == b'\xc3':
        return f'mov eax, {b[1]}; ret (constant)'
    if b[0] == 0xE9:
        return 'jmp thunk'
    if b[:3] == b'\x48\x8b\x01':
        return 'mov rax,[rcx] ... (trampoline-ish)'
    if b[:2] == b'\xc3':
        return 'ret (empty)'
    return ''


def main():
    if len(sys.argv) < 3:
        print('usage: vtable_scan.py <dll> <ClassName|substring> [slots]')
        return
    path, needle = sys.argv[1], sys.argv[2]
    nslots = int(sys.argv[3]) if len(sys.argv) > 3 else 48

    data = open(path, 'rb').read()
    image_base, sections = parse_pe(data)
    print(f'PE OK, image base 0x{image_base:x}, sections: '
          + ', '.join(f'{n}({hex(va)}+{hex(vs)})' for n, va, vs, _, _ in sections))

    tds = find_type_descriptors(data, sections, needle)
    if not tds:
        print(f'no TypeDescriptor for ".?AV{needle}..." found')
        return
    for td in tds:
        off = rva_to_off(td, sections)
        full = data[off + 16:off + 16 + 96].split(b'\0')[0].decode(errors='replace')
        print(f'\nTypeDescriptor @ RVA 0x{td:x}: {full}')

        cols = find_cols(data, sections, td)
        if not cols:
            print('  no COL found (abstract? skipped)')
            continue
        for col_rva, this_off in cols:
            cd_rva_off = rva_to_off(col_rva, sections)
            _, off_f, _, _, cd_rva, _ = struct.unpack_from('<IIIIII', data, cd_rva_off)
            num_bases = struct.unpack_from('<I', data, rva_to_off(cd_rva, sections))[0]
            print(f'  COL @ 0x{col_rva:x}, this-adjust 0x{this_off:x}, bases {num_bases}')
            for vft in find_vftables(data, sections, col_rva, image_base):
                print(f'  vftable @ RVA 0x{vft:x} (section {section_of(vft, sections)}):')
                v_off = rva_to_off(vft, sections)
                for slot in range(nslots):
                    q = struct.unpack_from('<Q', data, v_off + slot * 8)[0]
                    fn_rva = q - image_base if q >= image_base else q  # 兼容绝对 VA / RVA
                    if fn_rva == 0 or fn_rva >= 0x10000000:
                        if fn_rva >= 0x10000000:
                            txt = struct.pack('<Q', q).decode(errors='replace').rstrip('\0')
                            print(f'    [{slot:2d}] (data/str: {txt[:16]!r})')
                        else:
                            print(f'    [{slot:2d}] null')
                        continue
                    f_off = rva_to_off(fn_rva, sections)
                    if f_off is None:
                        print(f'    [{slot:2d}] RVA 0x{fn_rva:x} (unreadable)')
                        continue
                    b = data[f_off:f_off + 16]
                    note = classify(b)
                    hx = b.hex(' ')
                    print(f'    [{slot:2d}] RVA 0x{fn_rva:x}  {hx:<47} {note}')


if __name__ == '__main__':
    main()
