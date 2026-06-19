#!/usr/bin/env python3
"""Generate res/icon.png for Joe's Calibrage.

Flat Leaf-utility register (rounded green tile + border, like the SSH/File apps),
themed for the Zappa "Joe's Garage" pun: a garage roll-up door backdrop with an
analog thumbstick on a calibration target as the focus. Pure PIL, supersampled."""
import math
import os
from PIL import Image, ImageDraw

S = 256
SS = 4
N = S * SS
C = N / 2

# Leaf identity palette
GREEN    = (127, 176, 105)   # #7FB069 accent
GREEN_LT = (170, 210, 150)
GREEN_DK = (75, 110, 64)
TILE     = (22, 36, 15)      # #16240F dark base
SLAT     = (33, 50, 24)      # garage-door slat (just above the tile)
SLAT_HI  = (44, 62, 33)
INK      = (232, 241, 227)


def rounded(d, box, r, **kw):
    d.rounded_rectangle(box, radius=r, **kw)


def aa_circle(d, cx, cy, r, **kw):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], **kw)


img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# --- rounded tile + green border (Leaf utility look) -----------------------
inset = 16 * SS
rad = 50 * SS
rounded(d, [inset, inset, N - inset, N - inset], rad, fill=TILE)

# --- garage roll-up door: horizontal slat panels (clipped to the tile) -----
door = Image.new("RGBA", (N, N), (0, 0, 0, 0))
dd = ImageDraw.Draw(door)
top, bot = inset + 6 * SS, N - inset - 6 * SS
slats = 5
gap = (bot - top) / slats
for i in range(slats):
    y0 = top + i * gap
    rounded(dd, [inset + 6 * SS, y0 + 2 * SS, N - inset - 6 * SS, y0 + gap - 2 * SS],
            10 * SS, fill=SLAT)
    dd.line([inset + 10 * SS, y0 + 3 * SS, N - inset - 10 * SS, y0 + 3 * SS],
            fill=SLAT_HI, width=2 * SS)
mask = Image.new("L", (N, N), 0)
ImageDraw.Draw(mask).rounded_rectangle([inset, inset, N - inset, N - inset],
                                       radius=rad, fill=255)
img.paste(door, (0, 0), mask)
d = ImageDraw.Draw(img)

# --- calibration target: rings, crosshair, ticks ---------------------------
for r, a in ((92, 170), (66, 95)):
    aa_circle(d, C, C, r * SS, outline=GREEN + (a,), width=3 * SS)
inner, outer = 36 * SS, 92 * SS
for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
    d.line([C + dx * inner, C + dy * inner, C + dx * outer, C + dy * outer],
           fill=GREEN + (120,), width=3 * SS)
for k in range(8):
    ang = k * math.pi / 4
    r0, r1 = 92 * SS, (83 if k % 2 == 0 else 87) * SS
    ca, sa = math.cos(ang), math.sin(ang)
    d.line([C + ca * r0, C + sa * r0, C + ca * r1, C + sa * r1],
           fill=GREEN + (210,), width=3 * SS)

# --- soft shadow under the cap ---------------------------------------------
shadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
aa_circle(ImageDraw.Draw(shadow), C, C + 7 * SS, 46 * SS, fill=(0, 0, 0, 110))
img = Image.alpha_composite(img, shadow)
d = ImageDraw.Draw(img)

# --- stick cap (domed, concave thumb dish) ---------------------------------
cap_r = 46 * SS
grad = Image.new("RGBA", (N, N), (0, 0, 0, 0))
dg = ImageDraw.Draw(grad)
for i in range(int(cap_r * 2)):
    t = i / (cap_r * 2)
    col = tuple(int(GREEN_LT[j] + (GREEN_DK[j] - GREEN_LT[j]) * t) for j in range(3))
    dg.line([C - cap_r, C - cap_r + i, C + cap_r, C - cap_r + i], fill=col + (255,))
cmask = Image.new("L", (N, N), 0)
ImageDraw.Draw(cmask).ellipse([C - cap_r, C - cap_r, C + cap_r, C + cap_r], fill=255)
img.paste(grad, (0, 0), cmask)
d = ImageDraw.Draw(img)
aa_circle(d, C, C, 30 * SS, fill=GREEN_DK + (255,))
aa_circle(d, C, C - 4 * SS, 27 * SS, fill=GREEN + (255,))
aa_circle(d, C, C, cap_r, outline=GREEN_LT + (190,), width=3 * SS)
d.arc([C - 23 * SS, C - 25 * SS, C + 23 * SS, C + 10 * SS],
      start=200, end=340, fill=INK + (160,), width=4 * SS)

# --- tile border on top of everything --------------------------------------
rounded(d, [inset, inset, N - inset, N - inset], rad, outline=GREEN + (255,),
        width=6 * SS)

out = img.resize((S, S), Image.LANCZOS)
dst = os.path.join(os.path.dirname(__file__), "..", "res", "icon.png")
os.makedirs(os.path.dirname(dst), exist_ok=True)
out.save(dst)
print("wrote", os.path.normpath(dst), out.size, out.mode)
