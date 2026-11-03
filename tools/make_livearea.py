"""LiveArea images for the VPK (simple original art, not taken from the game).

Usage: python -E -P make_livearea.py <port_dir>     (writes port/vita/sce_sys/...)

The Vita wants 8-bit indexed PNGs: icon0 128x128, bg 840x500, startup 280x158.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

out = Path(sys.argv[1]) / "vita" / "sce_sys"
(out / "livearea" / "contents").mkdir(parents=True, exist_ok=True)
font = ImageFont.load_default()
BG, STONE, GOLD, RED, BALL = (24, 22, 34), (64, 58, 70), (240, 200, 80), (170, 40, 50), (70, 140, 230)


def text(img, xy, s, scale, color):
    w, h = font.getbbox(s)[2] + 1, font.getbbox(s)[3] + 1
    t = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(t).text((0, 0), s, font=font, fill=color + (255,))
    t = t.resize((w * scale, h * scale), Image.NEAREST)
    img.alpha_composite(t, (xy[0] - t.width // 2, xy[1]))


def bricks(img, size):
    d = ImageDraw.Draw(img)
    for y in range(0, img.height, size):
        off = (y // size % 2) * size
        for x in range(-size, img.width, size * 2):
            d.rectangle([x + off, y, x + off + size * 2 - 3, y + size - 3], fill=STONE)


def ball(img, cx, cy, r):
    d = ImageDraw.Draw(img)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=BALL, outline=(20, 30, 60), width=max(1, r // 6))
    d.ellipse([cx - r // 2, cy - r // 2, cx - r // 6, cy - r // 6], fill=(220, 240, 255))


def save(img, path):
    img.convert("RGB").quantize(64).save(path, optimize=True)


icon = Image.new("RGBA", (128, 128), BG + (255,))
bricks(icon, 16)
ImageDraw.Draw(icon).rectangle([8, 8, 119, 119], outline=GOLD, width=4)
ball(icon, 64, 56, 26)
text(icon, (64, 92), "DD", 3, GOLD)
save(icon, out / "icon0.png")

bg = Image.new("RGBA", (840, 500), BG + (255,))
bricks(bg, 40)
ImageDraw.Draw(bg).rectangle([0, 380, 840, 500], fill=(40, 30, 30))
for i in range(12):
    ImageDraw.Draw(bg).rectangle([i * 70 + 10, 395, i * 70 + 50, 405], fill=RED)
ball(bg, 640, 300, 50)
save(bg, out / "livearea" / "contents" / "bg.png")

st = Image.new("RGBA", (280, 158), BG + (255,))
bricks(st, 20)
text(st, (140, 40), "DUNGEON", 3, GOLD)
text(st, (140, 85), "DEATHBALL", 3, GOLD)
save(st, out / "livearea" / "contents" / "startup.png")

(out / "livearea" / "contents" / "template.xml").write_text("""<?xml version="1.0" encoding="utf-8"?>
<livearea style="a1" format-ver="01.00" content-rev="1">
  <livearea-background>
    <image>bg.png</image>
  </livearea-background>
  <gate>
    <startup-image>startup.png</startup-image>
  </gate>
</livearea>
""", encoding="utf-8")
print("LiveArea en", out)
