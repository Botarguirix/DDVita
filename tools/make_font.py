"""Placeholder HUD font: renders ASCII 32..126 with Pillow's built-in font into a .tex atlas.

Usage: python -E -P make_font.py <out_dir>     (writes font.tex and font.json)

Not game data (the original fonts are TTFs that the UI milestone will handle).
"""
import sys, json, struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

out = Path(sys.argv[1])
font = ImageFont.load_default()
chars = [chr(c) for c in range(32, 127)]
boxes = {c: font.getbbox(c) for c in chars}
height = max(b[3] for b in boxes.values()) + 1
cell_w = max(b[2] for b in boxes.values()) + 1
cols = 16
rows = (len(chars) + cols - 1) // cols
img = Image.new("RGBA", (cols * cell_w, rows * height), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)
glyphs = {}
for i, c in enumerate(chars):
    x, y = (i % cols) * cell_w, (i // cols) * height
    draw.text((x, y), c, font=font, fill=(255, 255, 255, 255))
    adv = font.getlength(c) if hasattr(font, "getlength") else boxes[c][2]
    # rect from the bottom-left of the texture (rows are flipped below)
    glyphs[str(ord(c))] = [x, img.height - y - height, cell_w, height, round(adv)]
flipped = img.transpose(Image.FLIP_TOP_BOTTOM)
with open(out / "font.tex", "wb") as f:
    f.write(b"DDTX" + struct.pack("<HH", flipped.width, flipped.height) + flipped.tobytes())
(out / "font.json").write_text(json.dumps({"height": height, "glyphs": glyphs}), encoding="utf-8")
print("font", img.size, "height", height)
