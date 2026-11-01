"""生成 MouseStatus 插件图标 res/icon.ico（鼠标轮廓，多尺寸）。"""
import os
from PIL import Image, ImageDraw, ImageFilter

S = 1024
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# 鼠标主体：胶囊形
bx0, by0, bx1, by1 = int(S * 0.235), int(S * 0.085), int(S * 0.765), int(S * 0.915)
radius = int((bx1 - bx0) * 0.5)

# 主体
body_color = (36, 110, 232, 255)
d.rounded_rectangle([bx0, by0, bx1, by1], radius=radius, fill=body_color)

# 顶部高光（半透明，仅覆盖上 30%，避免出现硬边）
hl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
dhl = ImageDraw.Draw(hl)
dhl.rounded_rectangle([bx0 + int(S * 0.02), by0 + int(S * 0.02),
                       bx1 - int(S * 0.02), by0 + int((by1 - by0) * 0.30)],
                      radius=radius, fill=(255, 255, 255, 32))
hl = hl.filter(ImageFilter.GaussianBlur(10))
img = Image.alpha_composite(img, hl)
d = ImageDraw.Draw(img)

# 滚轮（先算位置，分隔线只画到滚轮上方）
wheel_w = int(S * 0.125)
wheel_h = int(S * 0.135)
wy = by0 + int((by1 - by0) * 0.185)

# 按键分隔线：从顶部到滚轮
line_w = int(S * 0.030)
cx = S // 2
d.rectangle([cx - line_w // 2, by0 + int(S * 0.03), cx + line_w // 2, wy],
            fill=(255, 255, 255, 235))

# 滚轮本体
d.rounded_rectangle([cx - wheel_w // 2, wy, cx + wheel_w // 2, wy + wheel_h],
                    radius=wheel_w // 2, fill=(255, 255, 255, 255))
d.rounded_rectangle([cx - wheel_w // 2 + 7, wy + 7, cx + wheel_w // 2 - 7,
                     wy + wheel_h - 7], radius=wheel_w // 2, fill=(30, 96, 200, 255))

out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "res")
os.makedirs(out_dir, exist_ok=True)

sizes = [16, 24, 32, 48, 64, 128, 256]
frames = [img.resize((s, s), Image.LANCZOS) for s in sizes]
target = os.path.join(out_dir, "icon.ico")
frames[-1].save(target, format="ICO", sizes=[(s, s) for s in sizes],
                append_images=frames[:-1])
print("written:", target, os.path.getsize(target), "bytes")
