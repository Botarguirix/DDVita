"""LiveArea images, the boot image and the loading screen from the user's own Steam install.

Reads Steam's library cache (library_hero.jpg, logo.png) and the game's .exe icon (exported to a PNG
first, see below), writes vita/sce_sys_steam/ (git-ignored: game art is not part of the repo) and
vita/assets_steam/loading.tex. CMake packs them when present.

  python tools/make_livearea_steam.py <steam librarycache/861330 dir> <exe icon 256 png>
"""
import os, struct, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

cache, icon_png = sys.argv[1], sys.argv[2]
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, '..', 'vita', 'sce_sys_steam')
assets = os.path.join(here, '..', 'vita', 'assets_steam')
os.makedirs(os.path.join(out, 'livearea', 'contents'), exist_ok=True)
os.makedirs(assets, exist_ok=True)

hero = Image.open(os.path.join(cache, 'library_hero.jpg')).convert('RGBA')
logo_full = Image.open(os.path.join(cache, 'logo.png')).convert('RGBA')
# the two words and the rule under them (the tagline is replaced by "PSVITA")
words = logo_full.crop((40, 60, 600, 251))
FONT = r'C:\Windows\Fonts\arialbd.ttf'


def logo(width):
    """DUNGEON DEATHBALL / PSVITA, the Steam logo's style (white, thick black outline)."""
    k = width / words.width
    w = words.resize((width, round(words.height * k)), Image.LANCZOS)
    size = max(8, round(64 * k))
    font = ImageFont.truetype(FONT, size)
    stroke = max(2, round(10 * k))
    tmp = ImageDraw.Draw(Image.new('RGBA', (1, 1)))
    box = tmp.textbbox((0, 0), 'PSVITA', font=font, stroke_width=stroke)
    th = box[3] - box[1]
    img = Image.new("RGBA", (width, w.height + th + round(14 * k)), (0, 0, 0, 0))
    img.alpha_composite(w, (0, 0))
    d = ImageDraw.Draw(img)
    tw = box[2] - box[0]
    d.text(((width - tw) // 2 - box[0], w.height + round(8 * k) - box[1]), 'PSVITA', font=font, fill='white',
           stroke_width=stroke, stroke_fill='black')
    return img


def cover(img, w, h, focus_x=0.5, focus_y=0.4):
    """Scale to cover w x h, cropped around the focus point."""
    k = max(w / img.width, h / img.height)
    s = img.resize((round(img.width * k), round(img.height * k)), Image.LANCZOS)
    x = min(max(0, round(s.width * focus_x - w / 2)), s.width - w)
    y = min(max(0, round(s.height * focus_y - h / 2)), s.height - h)
    return s.crop((x, y, x + w, y + h))


def shade_bottom(img, start, alpha):
    """A dark gradient behind the logo so it reads on the art."""
    g = Image.new('L', (1, img.height))
    for y in range(img.height):
        t = max(0.0, (y - start) / max(1, img.height - start))
        g.putpixel((0, y), round(255 * alpha * min(1.0, t * 1.4)))
    dark = Image.new('RGBA', img.size, (12, 8, 20, 255))
    dark.putalpha(g.resize(img.size))
    img.alpha_composite(dark)


def save_indexed(img, path):  # the LiveArea wants 8-bit palette PNGs (0x8010113D otherwise)
    q = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:768]
    q.putpalette(pal + [0] * (768 - len(pal)))  # a full 256 entry palette: Pillow then writes 8 bits per pixel
    q.save(path)


def framed(w, h, logo_w, logo_y):
    img = cover(hero, w, h, 0.5, 0.0)
    shade_bottom(img, int(h * 0.45), 0.85)
    l = logo(logo_w)
    img.alpha_composite(l, ((w - l.width) // 2, logo_y if logo_y >= 0 else h - l.height + logo_y))
    return img


# LiveArea background (840x500): the monster up top, the logo centred in the lower half
save_indexed(framed(840, 500, 420, -40), os.path.join(out, 'livearea', 'contents', 'bg.png'))
# the gate's startup button (280x158)
gate = framed(280, 158, 200, -10)
save_indexed(gate, os.path.join(out, 'livearea', 'contents', 'startup.png'))
# pic0: shown while the game starts (960x544)
pic = framed(960, 544, 460, -48)
save_indexed(pic, os.path.join(out, 'pic0.png'))
# the bubble: the .exe's skull, point sampled
icon = Image.open(icon_png).convert('RGBA').resize((128, 128), Image.NEAREST)
save_indexed(icon, os.path.join(out, 'icon0.png'))
# the in-game loading screen: same art, bottom-row-first RGBA (the port's .tex)
load = framed(960, 544, 380, -120)
data = load.transpose(Image.FLIP_TOP_BOTTOM).tobytes()
with open(os.path.join(assets, 'loading.tex'), 'wb') as f:
    f.write(b'DDTX' + struct.pack('<HH', load.width, load.height) + data)
load.save(os.path.join(assets, 'loading_preview.png'))
print('ok')
