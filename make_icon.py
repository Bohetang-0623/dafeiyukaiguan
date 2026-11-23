# -*- coding: utf-8 -*-
"""最终图标：深海军蓝圆角底 + 微软 Fluent Emoji 鲸鱼（MIT）。
生成多尺寸 app.ico 与 亮/暗 背景的预览。"""
import os
from PIL import Image, ImageDraw, ImageFilter

DIR = os.path.dirname(os.path.abspath(__file__))
EMOJI = os.path.join(DIR, "emoji", "fluent_whale_3d.png")
OUT_ICO = os.path.join(DIR, "app.ico")
OUT_PREV = os.path.join(DIR, "icon_preview.png")
S = 1024
TOP = (40, 49, 88)      # 深海军蓝
BOT = (20, 25, 48)
SCALE = 0.78


def squircle():
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        gd.line([(0, y), (S, y)], fill=(
            int(TOP[0] + (BOT[0] - TOP[0]) * t),
            int(TOP[1] + (BOT[1] - TOP[1]) * t),
            int(TOP[2] + (BOT[2] - TOP[2]) * t), 255))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * .235), fill=255)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([int(-S * .15), int(-S * .3), int(S * .8), int(S * .38)],
                                 fill=(255, 255, 255, 26))
    glow = glow.filter(ImageFilter.GaussianBlur(S * .07))
    out = Image.alpha_composite(out, Image.composite(glow, Image.new("RGBA", (S, S), (0, 0, 0, 0)), mask))
    return out, mask


base, mask = squircle()
em = Image.open(EMOJI).convert("RGBA")
side = int(S * SCALE)
em = em.resize((side, side), Image.LANCZOS)
x = (S - side) // 2
y = (S - side) // 2
sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
sh.paste((0, 0, 0, 90), (x, y + int(S * .035)), em)
sh = sh.filter(ImageFilter.GaussianBlur(S * .03))
base = Image.alpha_composite(base, Image.composite(sh, Image.new("RGBA", (S, S), (0, 0, 0, 0)), mask))
base.alpha_composite(em, (x, y))
base.putalpha(Image.composite(base.split()[3], Image.new("L", (S, S), 0), mask))

sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
imgs = [base.resize((n, n), Image.LANCZOS) for n in sizes]
# 用最大那张开头保存，其它尺寸一起写进 ico
imgs[-1].save(OUT_ICO, format="ICO", sizes=[(n, n) for n in sizes])

# 预览：亮底 + 暗底，各放 256 / 48 / 32 / 16
W, H = 760, 320
prev = Image.new("RGBA", (W, H), (245, 247, 251, 255))
prev.paste((28, 32, 40, 255), (380, 0, W, H))
for bgx, label in ((0, "light"), (380, "dark")):
    prev.alpha_composite(base.resize((256, 256), Image.LANCZOS), (bgx + 18, 26))
    xx = bgx + 300
    for n in (48, 32, 16):
        prev.alpha_composite(base.resize((n, n), Image.LANCZOS), (xx, 120 - n // 2))
        xx += n + 16
prev.convert("RGB").save(OUT_PREV)
print("ico:", OUT_ICO, os.path.getsize(OUT_ICO), "字节")
print("preview:", OUT_PREV)
