# 把提取的武器 SVG 预光栅化为 RGBA 原始数据（.raw：uint32 w + uint32 h + RGBA8），
# 供 DLL 运行时直接建纹理，绕开游戏内部 SVG 解码函数（image_data_t::load_svg 链路，fail-fast 风险）
import re, struct, glob, io, os, shutil
import resvg_py
from PIL import Image

SRC = r'd:\Fatality-Framework-review\cs2_internal\x64\Debug\assets\icons\equipment'
DST = [
    r'd:\Fatality-Framework-review\cs2_internal\x64\Debug\assets\icons\equipment',
    r'D:\Fatality-Framework-review\x64\Debug\assets\icons\equipment',
]
TARGET_H = 12  # 与原 get_panorama_texture(…, 12) 一致

ok, fail = 0, []
for svg_path in glob.glob(os.path.join(SRC, '*.svg')):
    name = os.path.splitext(os.path.basename(svg_path))[0]
    try:
        # resvg 按原始尺寸渲染，再由 PIL 统一缩放到目标高度；
        # 宽度向上取整到 4 的倍数（透明补边）—— 保证 rowPitch = w*4 为 16 的倍数
        png = bytes(resvg_py.svg_to_bytes(svg_path=svg_path))
        img = Image.open(io.BytesIO(png)).convert('RGBA')
        w0, h0 = img.size
        w = max(1, round(w0 / h0 * TARGET_H))
        img = img.resize((w, TARGET_H), Image.LANCZOS)
        w4 = (w + 3) // 4 * 4
        if w4 != w:
            canvas = Image.new('RGBA', (w4, TARGET_H), (0, 0, 0, 0))
            canvas.paste(img, (0, 0))
            img = canvas
        w = w4

        data = struct.pack('<II', w, TARGET_H) + img.tobytes()
        for d in DST:
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, name + '.raw'), 'wb') as o:
                o.write(data)
        ok += 1
    except Exception as e:
        fail.append((name, repr(e)))

print(f'rasterized: {ok}, failed: {len(fail)}')
for n, r in fail[:10]:
    print('  FAIL', n, r)
