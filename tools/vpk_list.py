# 解析 VPK dir 索引，搜索匹配路径的文件（一次性工具）
import struct, sys, os

def parse_vpk(dir_path):
    files = []
    with open(dir_path, 'rb') as f:
        sig, ver, tree_size = struct.unpack('<III', f.read(12))
        if sig != 0x55AA1234:
            print(f'not vpk: {dir_path}')
            return files
        if ver == 2:
            f.read(16)  # file_data/archive_md5/other_md5/signature section sizes
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
                    pos += 16 + 2 + preload
                    files.append((f'{path_s}/{name_s}.{ext_s}', arc, off, length))
    return files

if __name__ == '__main__':
    base = r'E:\SOFTWARE\steam\steamapps\common\Counter-Strike Global Offensive\game'
    for sub in ['csgo/pak01_dir.vpk', 'core/pak01_dir.vpk']:
        p = os.path.join(base, sub)
        if not os.path.exists(p):
            print(f'missing: {p}')
            continue
        files = parse_vpk(p)
        hits = [f for f in files if 'equipment' in f[0].lower() and 'svg' in f[0]]
        print(f'{sub}: total={len(files)} svg_equipment={len(hits)}')
        for h in hits[:8]:
            print('  ', h)
