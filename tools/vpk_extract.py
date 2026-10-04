# 从 CS2 VPK 提取武器图标 vsvg_c 并明文化为 .svg（一次性工具）
import struct, os, glob

def parse_vpk(dir_path):
    files = []
    with open(dir_path, 'rb') as f:
        sig, ver, tree_size = struct.unpack('<III', f.read(12))
        if sig != 0x55AA1234:
            return files
        if ver == 2:
            f.read(16)
        tree = f.read(tree_size)
        pos = 0
        while pos < len(tree):
            ext = tree.index(b'\0', pos) - pos
            ext_s = tree[pos:pos+ext].decode()
            pos += ext + 1
            if not ext_s:
                break
            while pos < len(tree):
                path = tree.index(b'\0', pos) - pos
                path_s = tree[pos:pos+path].decode()
                pos += path + 1
                if not path_s:
                    break
                while pos < len(tree):
                    name = tree.index(b'\0', pos) - pos
                    name_s = tree[pos:pos+name].decode()
                    pos += name + 1
                    if not name_s:
                        break
                    crc, preload, arc, off, length = struct.unpack('<IHHII', tree[pos:pos+16])
                    pos += 16 + 2
                    preload_data = tree[pos:pos+preload]
                    pos += preload
                    files.append((f'{path_s}/{name_s}.{ext_s}', arc, off, length, preload_data))
    return files

def extract():
    vpk_dir = r'E:\SOFTWARE\steam\steamapps\common\Counter-Strike Global Offensive\game\csgo'
    out_root = r'd:\Fatality-Framework-review\cs2_internal\x64\Debug\assets\icons\equipment'
    os.makedirs(out_root, exist_ok=True)

    files = parse_vpk(os.path.join(vpk_dir, 'pak01_dir.vpk'))
    targets = [f for f in files if f[0].startswith('panorama/images/icons/equipment/') and f[0].endswith('.vsvg_c')]
    print(f'targets: {len(targets)}')

    # 卷文件缓存
    archives = {}
    ok, fail = 0, []
    for path, arc, off, length, preload in targets:
        if arc not in archives:
            ap = os.path.join(vpk_dir, f'pak01_{arc:03d}.vpk')
            if not os.path.exists(ap):
                fail.append((path, f'archive missing: {ap}'))
                continue
            archives[arc] = open(ap, 'rb')
        archives[arc].seek(off)
        data = archives[arc].read(length)
        if preload:
            data = preload + data

        # vsvg_c -> 明文 SVG：定位 <svg ... </svg>
        start = data.find(b'<svg ')
        end = data.rfind(b'</svg>')
        if start < 0 or end < 0:
            fail.append((path, 'svg markers not found'))
            continue
        svg = data[start:end + len(b'</svg>')]

        base = os.path.basename(path)[:-len('.vsvg_c')]
        with open(os.path.join(out_root, base + '.svg'), 'wb') as o:
            o.write(svg)
        ok += 1

    print(f'extracted: {ok}, failed: {len(fail)}')
    for f, r in fail[:10]:
        print('  FAIL', f, r)

if __name__ == '__main__':
    extract()
