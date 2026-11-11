"""docs/controls.png: the PS Vita controls of the port (Spanish / English)."""
import os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'controls.png')
W, H = 1600, 900
BOLD, REG, SYM = r'C:\Windows\Fonts\arialbd.ttf', r'C:\Windows\Fonts\arial.ttf', r'C:\Windows\Fonts\seguisym.ttf'
YEL, BLUE = (247, 226, 107), (140, 200, 255)


def font(size, bold=True):
    return ImageFont.truetype(BOLD if bold else REG, size)


img = Image.new('RGB', (W, H), (22, 18, 32))
d = ImageDraw.Draw(img)
d.text((W // 2, 50), 'Dungeon Deathball PSVITA - Controles / Controls', font=font(40), fill=YEL, anchor='mm')

# the console
d.rounded_rectangle((420, 170, 1180, 560), radius=150, fill=(40, 40, 48), outline=(120, 120, 135), width=4)
d.rectangle((570, 215, 1030, 515), fill=(10, 10, 14), outline=(90, 90, 100), width=3)
d.text((800, 340), 'PANTALLA TÁCTIL', font=font(26), fill=BLUE, anchor='mm')
d.text((800, 375), 'TOUCH SCREEN', font=font(26), fill=BLUE, anchor='mm')
d.text((800, 425), 'Info del equipo / Team info', font=font(22, False), fill='white', anchor='mm')
for x0, t in ((450, 'L'), (1030, 'R')):
    d.rounded_rectangle((x0, 148, x0 + 120, 178), radius=10, fill=(70, 70, 80))
    d.text((x0 + 60, 163), t, font=font(22), fill='white', anchor='mm')
cx, cy = 495, 300  # d-pad
d.rectangle((cx - 15, cy - 50, cx + 15, cy + 50), fill=(90, 90, 100))
d.rectangle((cx - 50, cy - 15, cx + 50, cy + 15), fill=(90, 90, 100))
d.ellipse((465, 400, 525, 460), fill=(25, 25, 30), outline=(110, 110, 120), width=3)
d.ellipse((1075, 400, 1135, 460), fill=(25, 25, 30), outline=(110, 110, 120), width=3)
fx, fy = 1105, 300
buttons = [(fx, fy - 45, (80, 200, 140), '△'), (fx - 45, fy, (230, 130, 200), '□'), (fx + 45, fy, (230, 90, 90), '○'),
           (fx, fy + 45, (110, 150, 240), '✕')]
for x, y, c, s in buttons:
    d.ellipse((x - 20, y - 20, x + 20, y + 20), fill=(30, 30, 36), outline=c, width=4)
    d.text((x, y), s, font=ImageFont.truetype(SYM, 24), fill=c, anchor='mm')
for x, t in ((1045, 'SELECT'), (1100, 'START')):
    d.rounded_rectangle((x - 20, 497, x + 20, 512), radius=6, fill=(90, 90, 100))
    d.text((x, 527), t, font=font(13), fill=(200, 200, 200), anchor='mm')


def label(x, y, tx, ty, text, right):
    d.line((x, y, tx, ty), fill=YEL, width=2)
    d.ellipse((x - 4, y - 4, x + 4, y + 4), fill=YEL)
    sx = tx + (8 if right else -8)
    if text[0] in '△○✕□':  # the button's symbol in a font that has it
        d.text((sx, ty), text[0], font=ImageFont.truetype(SYM, 24), fill=YEL, anchor='lm')
        sx += 30
        text = text[1:].strip()
    d.text((sx, ty), text, font=font(22, False), fill='white', anchor='lm' if right else 'rm')


label(510, 163, 390, 110, 'L: Cambiar jugador / Switch player', False)
label(1090, 163, 1210, 110, 'R: Popups de info / Info popups', True)
label(445, 300, 390, 230, 'Cruceta / D-pad: cursor', False)
label(465, 430, 390, 470, 'Stick izq.: cursor / Left stick: cursor', False)
label(fx + 20, fy - 45, 1210, 200, '△  Disparar / Shoot', True)
label(fx + 65, fy, 1210, 265, '○  Descansar, atrás / Rest, back', True)
label(fx + 20, fy + 45, 1210, 330, '✕  Acción, confirmar / Action, confirm', True)
label(fx - 65, fy, 1210, 395, '□  Pasar, tomar balón / Pass, take ball', True)
label(1045, 505, 1210, 465, 'SELECT: Deshacer / Undo', True)
label(1100, 505, 1210, 525, 'START: Pausa / Pause', True)

# the rear touchpad
d.rounded_rectangle((470, 620, 1130, 770), radius=30, fill=(32, 32, 40), outline=BLUE, width=3)
d.text((800, 665), 'PANEL TRASERO / REAR TOUCHPAD', font=font(28), fill=BLUE, anchor='mm')
d.text((800, 715), 'Toque: mostrar u ocultar las barras de vida', font=font(22, False), fill='white', anchor='mm')
d.text((800, 745), 'Tap: show or hide the health bars', font=font(22, False), fill='white', anchor='mm')
d.text((W // 2, 840), 'START + SELECT: registro y rendimiento / log and performance', font=font(22, False), fill=(180, 180, 190), anchor='mm')
os.makedirs(os.path.dirname(OUT), exist_ok=True)
img.save(OUT)
print(OUT)
