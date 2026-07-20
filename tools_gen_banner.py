#!/usr/bin/env python3
"""Render the Aurora GitHub banner + social-preview card.

The motif is the product and the name in one picture: a spectrum trace forming
a horizon, curtains of light rising out of its peaks the way an aurora rises
out of the skyline - green at the base, cyan and violet as it climbs - and the
real 1-bit dithered waterfall scrolling away underneath. Every curtain is
anchored to a peak in the trace, so the artwork is the data.

Type is auto-fitted to its column rather than hand-tuned, so a wording change
can never quietly overflow into the artwork. Supersampled, then
LANCZOS-downsampled.
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
BLACK_F = "/System/Library/Fonts/Supplemental/Arial Black.ttf"
MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
REG = "/System/Library/Fonts/Supplemental/Arial.ttf"

# palette - night sky, aurora light
BG_TOP = (4, 6, 13)
BG_BOT = (10, 16, 31)
GREEN = (110, 255, 182)  # the curtain's base
CYAN = (72, 206, 255)  # mid climb
VIOLET = (196, 138, 255)  # the top of the curtain
WHITE = (240, 246, 252)
GRAY = (152, 165, 182)
DIM = (34, 46, 62)

SS = 2  # supersample
BAYER4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]


def add(a, b):
    """Additive compositing - light stacks, it does not replace."""
    return ImageChops.add(a, b)


def font(path, px):
    try:
        return ImageFont.truetype(path, int(px))
    except OSError:
        return ImageFont.truetype(BOLD, int(px))


def fit(path, px, text, max_w):
    """Largest size at or under `px` whose `text` fits `max_w`.

    The banner is regenerated whenever the pitch is reworded, and a line that
    silently runs into the artwork is the classic way a generated banner rots.
    """
    size = int(px)
    while size > 8:
        f = font(path, size)
        if f.getlength(text) <= max_w:
            return f
        size -= 1
    return font(path, 8)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def ramp(t):
    """Green at the horizon, violet at the top - the real aurora sequence."""
    if t < 0.55:
        return lerp(GREEN, CYAN, t / 0.55)
    return lerp(CYAN, VIOLET, (t - 0.55) / 0.45)


def vgradient(w, h):
    img = Image.new("RGB", (w, h), BG_TOP)
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(BG_TOP, BG_BOT, y / max(1, h - 1)))
    return img


# ---------------- the signal ----------------

# Carriers as (position across the frame, strength, width). These are what the
# curtains grow out of, so the composition is driven by the trace, not the
# other way round.
CARRIERS = [
    (0.06, 0.34, 0.028),
    (0.17, 0.66, 0.016),
    (0.26, 0.24, 0.038),
    (0.38, 1.00, 0.012),
    (0.47, 0.46, 0.020),
    (0.58, 0.82, 0.015),
    (0.68, 0.30, 0.032),
    (0.79, 0.94, 0.014),
    (0.90, 0.42, 0.024),
]


class LCG:
    """Deterministic grass, so the artwork never churns in git."""

    def __init__(self, seed=0xA57A):
        self.s = seed

    def unit(self):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.s >> 8) / 0x7FFFFF


def trace(n, phase=0.0):
    """Signal strength 0..1 across `n` samples."""
    rng = LCG(0xA57A + int(abs(phase) * 977))
    out = []
    for x in range(n):
        u = x / max(1, n - 1)
        v = 0.05 + 0.05 * rng.unit()
        for cx, amp, wd in CARRIERS:
            # Carriers breathe over time so waterfall rows differ from row to
            # row - but phase 0 is the reference sweep that the curtains and
            # the horizon are built from, and there every carrier stands at
            # full height. Breathing it too was dimming half the skyline.
            a = amp if phase == 0.0 else amp * (0.55 + 0.45 * math.sin(phase * 2.4 + cx * 19.0))
            v = max(v, a * math.exp(-(((u - cx) / wd) ** 2)))
        out.append(min(1.0, v))
    return out


def side_mask(w, x0, x1):
    """0 left of x0, 1 right of x1, smooth between - keeps the art off the type."""
    m = []
    for x in range(w):
        if x <= x0:
            m.append(0.0)
        elif x >= x1:
            m.append(1.0)
        else:
            t = (x - x0) / (x1 - x0)
            m.append(t * t * (3 - 2 * t))  # smoothstep
    return m


# ---------------- layers ----------------


def build_curtains(w, h, base_y, tr, mask, sky_top):
    """Vertical light rising out of each peak, fading as it climbs."""
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    px = layer.load()
    reach = base_y - sky_top

    for x in range(w):
        s = tr[x] * mask[x]
        if s < 0.06:
            continue
        span = reach * (0.28 + 0.72 * s)
        top = max(0, int(base_y - span))
        if span <= 1:
            continue
        for y in range(top, base_y):
            # 0 at the horizon, 1 at the tip. Clamped: `span` is fractional, so
            # the first row can sit a hair above it, and a negative base to a
            # fractional power is a complex number rather than a dim pixel.
            t = min(1.0, max(0.0, (base_y - y) / span))
            a = (1.0 - t) ** 1.35 * s * 1.35
            if a <= 0.004:
                continue
            c = ramp(t)
            px[x, y] = (
                min(255, int(c[0] * a)),
                min(255, int(c[1] * a)),
                min(255, int(c[2] * a)),
            )
    return layer


def build_waterfall(w, h, top, rows, cell, tr_rows, mask):
    """The actual 1-bit dithered waterfall, in aurora colour."""
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    gap = max(1, cell // 4)

    for r in range(rows):
        tr = tr_rows[r]
        fade = 1.0 - (r / rows) * 0.78
        y = top + r * cell
        if y + cell >= h:
            break
        for cx in range(w // cell):
            x = cx * cell
            s = tr[min(w - 1, x)] * mask[min(w - 1, x)]
            # Lift the ramp before dithering. On a 128 px screen the true noise
            # floor is a faint living texture; at banner scale the same density
            # scatters into unrelated dots, so the floor is raised until the
            # rows read as rows.
            lvl = int(round((0.22 + 0.78 * s) * 16)) if s > 0.01 else 0
            if lvl <= BAYER4[((r & 3) << 2) | (cx & 3)]:
                continue
            c = ramp(min(1.0, s * 0.85))
            d.rectangle(
                [x, y, x + cell - gap, y + cell - gap],
                fill=tuple(int(v * fade) for v in c),
            )
    return layer


# ---------------- composition ----------------


def render(path, W, H, layout="wide"):
    w, h = W * SS, H * SS
    img = vgradient(w, h)

    # ---- stars ----
    rng = LCG(0x51A45)
    sd = ImageDraw.Draw(img)
    for _ in range(int(w * h / 4200)):
        x, y = int(rng.unit() * w), int(rng.unit() * h * 0.7)
        v = int(60 + rng.unit() * 140)
        sd.point((x, y), fill=(v, v, min(255, v + 22)))

    pad = 70 * SS
    foot_band = 74 * SS  # reserved strip along the bottom for the footer

    if layout == "wide":
        # Art lives in the right half; the left half is the type column.
        mask = side_mask(w, int(w * 0.49), int(w * 0.62))
        text_w = int(w * 0.47) - pad
        base_y = int(h * 0.54)
        sky_top = int(h * 0.06)
        wf_rows, cell = 9, max(3, int(h * 0.019))
    else:
        mask = [1.0] * w
        text_w = w - pad * 2
        base_y = int(h * 0.28)
        sky_top = int(h * 0.04)
        wf_rows, cell = 7, max(3, int(h * 0.015))

    tr = trace(w, phase=0.0)

    # ---- curtains: three additive passes, wide halo to sharp core ----
    curtains = build_curtains(w, h, base_y, tr, mask, sky_top)
    for blur in (22 * SS, 8 * SS, 0):
        lay = curtains.filter(ImageFilter.GaussianBlur(blur)) if blur else curtains
        img = add(img, lay)

    # ---- the trace itself: a bright horizon ----
    horizon = Image.new("RGB", (w, h), (0, 0, 0))
    hp = horizon.load()
    for x in range(w):
        if mask[x] <= 0.01:
            continue
        y = base_y - int(tr[x] * (base_y - sky_top) * 0.26)
        for k in range(2 * SS):
            if 0 <= y + k < h:
                c = ramp(0.0)
                hp[x, y + k] = tuple(min(255, int(v * mask[x])) for v in c)
    img = add(img, horizon.filter(ImageFilter.GaussianBlur(4 * SS)))
    img = add(img, horizon)

    # ---- waterfall under the horizon ----
    wf_top = base_y + cell * 2
    max_rows = max(0, (h - foot_band - wf_top) // cell)
    tr_rows = [trace(w, phase=-(r + 1) * 0.42) for r in range(min(wf_rows, max_rows))]
    if tr_rows:
        wf = build_waterfall(w, h, wf_top, len(tr_rows), cell, tr_rows, mask)
        img = add(img, wf.filter(ImageFilter.GaussianBlur(3 * SS)))
        img = add(img, wf)

    # ---- footer band, so the small type is always legible ----
    fb = Image.new("RGB", (w, h), (0, 0, 0))
    ImageDraw.Draw(fb).rectangle([0, h - foot_band, w, h], fill=(255, 255, 255))
    img = Image.composite(Image.new("RGB", (w, h), (4, 6, 12)), img, fb.convert("L"))

    # ---- type ----
    tx = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    td = ImageDraw.Draw(tx)

    kicker = "FLIPPER ZERO  ·  BAND WATERFALL SCANNER"
    tagline = "See what is on the air around you."
    sub = "Live spectrum over a scrolling waterfall — no extra hardware."

    f_kick = fit(MONO, 22 * SS, kicker, text_w)
    f_title = fit(BLACK_F, 122 * SS, "AURORA", text_w)
    f_tag = fit(BOLD, 36 * SS, tagline, text_w)
    f_sub = fit(REG, 23 * SS, sub, text_w)
    f_foot = font(MONO, 21 * SS)

    title_px = f_title.size
    if layout == "wide":
        kicker_y = int(h * 0.20)
    else:
        kicker_y = int(h * 0.47)
    title_y = kicker_y + int(30 * SS)

    td.text((pad, kicker_y), kicker, font=f_kick, fill=CYAN)
    td.text((pad + 4 * SS, title_y + 4 * SS), "AURORA", font=f_title, fill=GREEN + (110,))
    td.text((pad, title_y), "AURORA", font=f_title, fill=WHITE)

    tag_y = title_y + title_px + int(18 * SS)
    td.text((pad, tag_y), tagline, font=f_tag, fill=GREEN)
    td.text((pad, tag_y + int(f_tag.size * 1.35)), sub, font=f_sub, fill=GRAY)

    img = Image.alpha_composite(img.convert("RGBA"), tx).convert("RGB")

    fd = ImageDraw.Draw(img)
    fd.line([(pad, h - foot_band + 14 * SS), (w - pad, h - foot_band + 14 * SS)], fill=DIM, width=2 * SS)
    fd.text((pad, h - 44 * SS), "github.com/at0m-b0mb/Aurora-FlipperZero", font=f_foot, fill=GRAY)
    fd.text((w - pad, h - 44 * SS), "MIT · by at0m-b0mb", font=f_foot, fill=GRAY, anchor="ra")

    img.resize((W, H), Image.LANCZOS).save(path)
    print("wrote", path)


if __name__ == "__main__":
    render(os.path.join(OUT, "banner.png"), 1280, 400, layout="wide")
    render(os.path.join(OUT, "social-preview.png"), 1280, 640, layout="card")
