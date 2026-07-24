#!/usr/bin/env python3
"""Render Flipper-style mock screenshots (128x64, orange backlight) for the README.

These mirror views/scanner_view.c line for line - the same layout constants,
the same Bayer dither, the same bin/zoom arithmetic from helpers/aur_scale.c -
so a layout collision or an unreadable waterfall shows up here before it ships
on a device. Text is positioned by BASELINE (PIL anchor "ls"/"rs"/"ms"/"mm")
because canvas_draw_str takes y as the baseline.

The RF is synthesised, not captured: a noise floor, a few carriers with the
270 kHz smear the CC1101's receive filter really imposes, and a burst pattern
in the waterfall history.
"""
from PIL import Image, ImageDraw, ImageFont
import math
import os

S = 6  # upscale factor
W, H = 128, 64
BG = (255, 130, 0)  # flipper backlight orange
FG = (10, 8, 4)  # near-black pixels
OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

f_sec = ImageFont.truetype(MONO, 7 * S - 2)  # FontSecondary
f_pri = ImageFont.truetype(BOLD, 8 * S)  # FontPrimary

# ---------------- layout, mirrored from views/scanner_view.c ----------------

SV_HDR_BASE = 9
SV_RULE_Y = 11
SV_TOP = 13

SV_STRIP_Y = 53
SV_STRIP_H = 11
SV_STRIP_BASE = 62

SV_SPLIT_AXIS = 31
SV_SPLIT_WF_TOP = 33
SV_SPLIT_WF_ROWS = 20

SV_WF_TOP = SV_TOP
SV_WF_ROWS = 40

SV_SPECT_HITS = 12
SV_SPECT_TOP = 16
SV_SPECT_AXIS = 50

# ---------------- helpers/aur_scale.c, mirrored ----------------

AUR_BINS = 64
AUR_LEVELS = 17
AUR_MIN_SPAN = 2_000_000
AUR_ZOOM_LIMIT = 8
DBM_INVALID = -127

BANDS = [
    (300_000_000, 348_000_000, "300-348", "315 ISM"),
    (387_000_000, 464_000_000, "387-464", "433 ISM"),
    (779_000_000, 928_000_000, "779-928", "868/915"),
]

BAYER4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]


def zoom_max(band):
    lo, hi, _, _ = BANDS[band]
    w = hi - lo
    z = 0
    while z < AUR_ZOOM_LIMIT and (w >> (z + 1)) >= AUR_MIN_SPAN:
        z += 1
    return z


def span_for(band, zoom):
    lo, hi, _, _ = BANDS[band]
    return (hi - lo) >> min(zoom, zoom_max(band))


class Plan:
    def __init__(self, band, zoom=0, focus=None):
        lo, hi, _, _ = BANDS[band]
        self.band = band
        self.zoom = min(zoom, zoom_max(band))
        self.span = span_for(band, self.zoom)
        self.center = focus if focus is not None else lo + (hi - lo) // 2
        self._clamp()

    def _clamp(self):
        lo, hi, _, _ = BANDS[self.band]
        half_lo = self.span // 2
        half_hi = self.span - half_lo
        self.center = max(lo + half_lo, min(hi - half_hi, self.center))

    @property
    def low(self):
        return self.center - self.span // 2

    @property
    def bin_width(self):
        return max(1, self.span // AUR_BINS)

    def bin_freq(self, b):
        b = min(b, AUR_BINS - 1)
        bw = self.bin_width
        return self.low + bw * b + bw // 2

    def freq_bin(self, f):
        if f <= self.low:
            return 0
        return min(AUR_BINS - 1, (f - self.low) // self.bin_width)


def tick_step(span):
    for s in (100_000, 200_000, 500_000, 1_000_000, 2_000_000,
              5_000_000, 10_000_000, 20_000_000, 50_000_000, 100_000_000):
        if span // s <= 8:
            return s
    return 100_000_000


def scale_px(dbm, floor, range_db, height):
    if height == 0 or dbm <= floor:
        return 0
    d = dbm - floor
    if d >= range_db:
        return height
    return (d * height) // range_db


AUR_FLOOR_BIAS_DB = 6


def display_px(dbm, floor, range_db, height):
    """Zero point sits a few dB under the floor so noise keeps a live texture;
    the range gets the same amount back so the top of scale does not move."""
    return scale_px(dbm, floor - AUR_FLOOR_BIAS_DB, range_db + AUR_FLOOR_BIAS_DB, height)


def level(dbm, floor, range_db):
    return display_px(dbm, floor, range_db, AUR_LEVELS - 1)


def dither(lvl, x, y):
    return lvl > BAYER4[((y & 3) << 2) | (x & 3)]


# ---------------- canvas primitives ----------------


def canvas():
    img = Image.new("RGB", (W * S, H * S), BG)
    return img, ImageDraw.Draw(img)


def L(v):
    return int(round(v * S))


def dot(d, x, y, col=FG):
    d.rectangle([L(x), L(y), L(x + 1) - 1, L(y + 1) - 1], fill=col)


def line(d, x0, y0, x1, y1, col=FG):
    """canvas_draw_line, restricted to the H/V cases this view uses."""
    if y0 == y1:
        d.rectangle([L(min(x0, x1)), L(y0), L(max(x0, x1) + 1) - 1, L(y0 + 1) - 1], fill=col)
    else:
        d.rectangle([L(x0), L(min(y0, y1)), L(x0 + 1) - 1, L(max(y0, y1) + 1) - 1], fill=col)


def box(d, x, y, w, h, col=FG):
    if w <= 0 or h <= 0:
        return
    d.rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], fill=col)


def frame(d, x, y, w, h, col=FG, lw=2):
    d.rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], outline=col, width=lw)


def rbox(d, x, y, w, h, r, col=FG):
    d.rounded_rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], radius=L(r), fill=col)


def rframe(d, x, y, w, h, r, col=FG, lw=2):
    d.rounded_rectangle([L(x), L(y), L(x + w) - 1, L(y + h) - 1], radius=L(r), outline=col, width=lw)


def text(d, x, y, s, fnt=f_sec, col=FG, anchor="ls"):
    """y is the BASELINE, matching canvas_draw_str."""
    d.text((L(x), L(y)), s, font=fnt, fill=col, anchor=anchor)


def tw(s, fnt=f_sec):
    return fnt.getlength(s) / S


def save(img, name):
    p = os.path.join(OUT, name)
    img.save(p)
    print("wrote", p)


# ---------------- formatting, mirrored ----------------


def fmt_mhz3(hz):
    return "%d.%03d" % (hz // 1_000_000, (hz % 1_000_000) // 1000)


def fmt_mhz2(hz):
    return "%d.%02d" % (hz // 1_000_000, (hz % 1_000_000) // 10_000)


def fmt_span(hz):
    return "%d.%dM" % (hz // 1_000_000, (hz % 1_000_000) // 100_000)


def fmt_delta(hz):
    """Mirror of aur_fmt_delta_hz: kHz below a megahertz, MHz above."""
    sign = "-" if hz < 0 else "+"
    khz = (abs(hz) + 500) // 1000
    if khz >= 1000:
        return "%s%d.%02dM" % (sign, khz // 1000, (khz % 1000) // 10)
    return "%s%dk" % (sign, khz)


# ---------------- synthetic RF ----------------

FLOOR = -97
RANGE = 50


class LCG:
    """Deterministic noise, so the README images never churn in git."""

    def __init__(self, seed=0x5EED):
        self.s = seed

    def next(self):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return self.s

    def jitter(self, amp):
        return (self.next() % (2 * amp + 1)) - amp


def sweep(plan, carriers, rng, gain=1.0):
    """One sweep: a noise floor plus carriers smeared by the receive filter.

    The width is not decorative. The CC1101's narrowest stock filter is 270 kHz,
    so a carrier occupies 270000/bin_width bins however far you zoom - which is
    exactly why Aurora stops zooming at 2 MHz.
    """
    filt_bins = max(1.1, 270_000 / plan.bin_width)
    out = []
    for i in range(AUR_BINS):
        v = FLOOR + rng.jitter(2)
        for cf, cdbm, extra in carriers:
            cb = (cf - plan.low) / plan.bin_width
            wdt = filt_bins * extra
            g = math.exp(-(((i - cb) / wdt) ** 2))
            v = max(v, FLOOR + (cdbm - FLOOR) * g * gain)
        out.append(int(round(v)))
    return out


def history(plan, carriers, bursts, rows=SV_WF_ROWS, seed=0x1234):
    """Waterfall history, newest first. `bursts` gates each carrier per row."""
    rng = LCG(seed)
    hist = []
    for r in range(rows):
        active = [c for n, c in enumerate(carriers) if bursts[n](r)]
        hist.append(sweep(plan, active, rng))
    return hist


# ---------------- faces, mirrored from scanner_view.c ----------------


def draw_header(d, cursor_freq, cursor_dbm):
    text(d, 2, SV_HDR_BASE, fmt_mhz3(cursor_freq), f_pri)
    s = "-- dBm" if cursor_dbm == DBM_INVALID else "%d dBm" % cursor_dbm
    text(d, 126, SV_HDR_BASE, s, f_sec, anchor="rs")
    line(d, 0, SV_RULE_Y, 127, SV_RULE_Y)


def draw_caret_up(d, x, y, col=FG):
    for i in range(4):
        line(d, x - i, y + i, x + i, y + i, col)


def draw_strip(d, plan, strongest, hold=False, delta=None):
    box(d, 0, SV_STRIP_Y, W, SV_STRIP_H)
    if hold:
        rbox(d, 2, 54, 27, 9, 2, BG)
        text(d, 6, 61, "HOLD", f_sec, FG)
    else:
        text(d, 3, SV_STRIP_BASE, fmt_span(plan.span), f_sec, BG)

    if delta is not None:
        dhz, ddb = delta
        s = fmt_delta(dhz) if ddb is None else "%s %+d" % (fmt_delta(dhz), ddb)
        text(d, 125, SV_STRIP_BASE, s, f_sec, BG, anchor="rs")
        w = tw(s)
        tabx = 125 - w - 6
        box(d, tabx, 55, 4, 4, BG)
        frame(d, tabx, 55, 4, 4, FG, lw=2)
    elif strongest is not None:
        sbin, sdbm = strongest
        s = "%s %d" % (fmt_mhz2(plan.bin_freq(sbin)), sdbm)
        text(d, 125, SV_STRIP_BASE, s, f_sec, BG, anchor="rs")
        draw_caret_up(d, 125 - tw(s) - 5, 56, BG)


def draw_marker(d, mx, top, bottom):
    """Mirror of the C: a coarse two-tone dashed guide with a top tab."""
    box(d, mx - 1, top, 4, 3, BG)
    frame(d, mx - 1, top, 4, 3, FG, lw=2)
    y = top + 3
    while y <= bottom:
        dot(d, mx, y, BG)
        if y + 1 <= bottom:
            dot(d, mx, y + 1, BG)
        dot(d, mx + 1, y, FG)
        if y + 1 <= bottom:
            dot(d, mx + 1, y + 1, FG)
        y += 4


def draw_axis(d, plan, axis):
    for x in range(0, W, 2):
        dot(d, x, axis)
    step = tick_step(plan.span)
    lo = plan.low
    hz_per_px = max(1, plan.span // W)
    first = ((lo + step - 1) // step) * step
    f = first
    while f <= lo + plan.span:
        x = (f - lo) // hz_per_px
        if x < W:
            line(d, x, axis - 1, x, axis + 1)
        f += step


def draw_spectrum(d, plan, dbm, peak, cursor_bin, top, axis, marker_bin=None):
    height = axis - top
    for i in range(AUR_BINS):
        x = i * 2
        h = display_px(dbm[i], FLOOR, RANGE, height)
        if h > 0:
            box(d, x, axis - h, 2, h)
        if peak is not None:
            ph = display_px(peak[i], FLOOR, RANGE, height)
            if ph > h + 1:
                box(d, x, axis - ph, 2, 1)

    draw_axis(d, plan, axis)

    if marker_bin is not None:
        draw_marker(d, marker_bin * 2, top, axis - 1)

    cx = cursor_bin * 2
    for y in range(top, axis - 1, 2):
        dot(d, cx, y)
    box(d, cx, axis - 1, 2, 3)


def draw_waterfall(d, hist, cursor_bin, top, rows, time_ticks=False, rate10=140, marker_bin=None):
    drawn = min(rows, len(hist))
    for r in range(drawn):
        y = top + r
        src = hist[r]
        for i in range(AUR_BINS):
            lvl = level(src[i], FLOOR, RANGE)
            if not lvl:
                continue
            x = i * 2
            if dither(lvl, x, y):
                dot(d, x, y)
            if dither(lvl, x + 1, y):
                dot(d, x + 1, y)

    if marker_bin is not None:
        draw_marker(d, marker_bin * 2, top, top + rows - 1)

    cx = cursor_bin * 2
    for y in range(top, top + rows, 2):
        dot(d, cx, y, BG)
    for y in range(top + 1, top + rows, 2):
        dot(d, cx + 1, y, FG)

    if not time_ticks or rate10 < 5:
        return
    s = 1
    while True:
        y = top + (s * rate10) // 10
        if y >= top + rows:
            break
        line(d, 123, y, 127, y, BG)
        line(d, 126, y, 127, y, FG)
        s += 1


def draw_hits(d, hits, y):
    for i in range(AUR_BINS):
        if hits[i]:
            box(d, i * 2, y, 2, 2)


def draw_hint(d):
    rbox(d, 3, 26, 122, 26, 3, BG)
    rframe(d, 3, 26, 122, 26, 3, FG)
    text(d, 64, 33, "<> cursor   ^v zoom", f_sec, FG, anchor="mm")
    text(d, 64, 41, "OK view  hold OK snap", f_sec, FG, anchor="mm")
    text(d, 64, 49, "hold ^ = drop marker", f_sec, FG, anchor="mm")


# ---------------- scenes ----------------

# A plausible 433 MHz neighbourhood: a garage remote hammering away, a PMR446
# handheld, a weak telemetry link and a wideband industrial hum.
CARRIERS_WIDE = [
    (433_920_000, -46, 1.0),
    (446_050_000, -63, 1.0),
    (392_500_000, -81, 1.0),
    (458_000_000, -71, 2.4),
]


def render_split():
    img, d = canvas()
    plan = Plan(1)
    rng = LCG(0xA11CE)
    dbm = sweep(plan, CARRIERS_WIDE, rng)
    peak = [v + (3 if i % 5 else 5) for i, v in enumerate(dbm)]

    strongest = max(range(AUR_BINS), key=lambda i: dbm[i])
    cursor = strongest  # the hero shot: header and marker agreeing on one signal

    bursts = [
        lambda r: (r // 6) % 2 == 0,  # the remote, pulsing
        lambda r: True,
        lambda r: r % 3 != 0,
        lambda r: True,
    ]
    hist = history(plan, CARRIERS_WIDE, bursts)

    draw_header(d, plan.bin_freq(cursor), dbm[cursor])
    draw_spectrum(d, plan, dbm, peak, cursor, SV_TOP, SV_SPLIT_AXIS)
    draw_waterfall(d, hist, cursor, SV_SPLIT_WF_TOP, SV_SPLIT_WF_ROWS)
    draw_strip(d, plan, (strongest, dbm[strongest]))
    save(img, "screen_split.png")


def render_waterfall():
    img, d = canvas()
    plan = Plan(1)
    rng = LCG(0xB0B)
    dbm = sweep(plan, CARRIERS_WIDE, rng)
    cursor = plan.freq_bin(433_920_000)
    strongest = max(range(AUR_BINS), key=lambda i: dbm[i])

    bursts = [
        lambda r: (r // 4) % 3 != 2,
        lambda r: True,
        lambda r: r % 2 == 0,
        lambda r: r > 8,
    ]
    hist = history(plan, CARRIERS_WIDE, bursts, seed=0xC0FFEE)

    draw_header(d, plan.bin_freq(cursor), dbm[cursor])
    draw_waterfall(d, hist, cursor, SV_WF_TOP, SV_WF_ROWS, time_ticks=True, rate10=140)
    draw_strip(d, plan, (strongest, dbm[strongest]))
    save(img, "screen_waterfall.png")


def render_spectrum():
    img, d = canvas()
    plan = Plan(1)
    rng = LCG(0xD00D)
    dbm = sweep(plan, CARRIERS_WIDE, rng)
    peak = [v + (4 if i % 4 else 7) for i, v in enumerate(dbm)]
    cursor = plan.freq_bin(446_050_000)
    strongest = max(range(AUR_BINS), key=lambda i: dbm[i])
    hits = [peak[i] - FLOOR >= 12 for i in range(AUR_BINS)]  # AUR_HIT_DB

    draw_header(d, plan.bin_freq(cursor), dbm[cursor])
    draw_hits(d, hits, SV_SPECT_HITS)
    draw_spectrum(d, plan, dbm, peak, cursor, SV_SPECT_TOP, SV_SPECT_AXIS)
    draw_strip(d, plan, (strongest, dbm[strongest]))
    save(img, "screen_spectrum.png")


def render_zoom():
    """Zoomed onto one carrier. The hump is 7 bins wide because the receive
    filter is 270 kHz and the bins are 37 kHz - the honest picture."""
    img, d = canvas()
    plan = Plan(1, zoom=5, focus=433_920_000)
    rng = LCG(0xFEED)
    carriers = [(433_920_000, -46, 1.0), (433_400_000, -74, 1.0)]
    dbm = sweep(plan, carriers, rng)
    peak = [v + (3 if i % 6 else 6) for i, v in enumerate(dbm)]
    cursor = plan.freq_bin(433_920_000)
    strongest = max(range(AUR_BINS), key=lambda i: dbm[i])

    bursts = [lambda r: (r // 5) % 2 == 0, lambda r: True]
    hist = history(plan, carriers, bursts, seed=0x7A7A)

    draw_header(d, plan.bin_freq(cursor), dbm[cursor])
    draw_spectrum(d, plan, dbm, peak, cursor, SV_TOP, SV_SPLIT_AXIS)
    draw_waterfall(d, hist, cursor, SV_SPLIT_WF_TOP, SV_SPLIT_WF_ROWS)
    draw_strip(d, plan, (strongest, dbm[strongest]))
    save(img, "screen_zoom.png")


def render_hold():
    img, d = canvas()
    plan = Plan(1, zoom=3, focus=433_920_000)
    rng = LCG(0x5150)
    carriers = [(433_920_000, -44, 1.0), (431_800_000, -68, 1.6)]
    dbm = sweep(plan, carriers, rng)
    cursor = plan.freq_bin(433_920_000)
    strongest = max(range(AUR_BINS), key=lambda i: dbm[i])
    bursts = [lambda r: 4 < r < 14, lambda r: True]
    hist = history(plan, carriers, bursts, seed=0x2B2B)

    draw_header(d, plan.bin_freq(cursor), dbm[cursor])
    draw_spectrum(d, plan, dbm, None, cursor, SV_TOP, SV_SPLIT_AXIS)
    draw_waterfall(d, hist, cursor, SV_SPLIT_WF_TOP, SV_SPLIT_WF_ROWS)
    draw_strip(d, plan, (strongest, dbm[strongest]), hold=True)
    save(img, "screen_hold.png")


def render_marker():
    """Marker on one carrier, cursor on another: the strip reads the Δf / ΔdB
    between them - here, the spacing between two 433-band signals."""
    img, d = canvas()
    plan = Plan(1, zoom=2, focus=440_000_000)
    rng = LCG(0x11A6)
    carriers = [(433_920_000, -44, 1.0), (446_050_000, -58, 1.0), (438_800_000, -80, 1.3)]
    dbm = sweep(plan, carriers, rng)
    peak = [v + (3 if i % 5 else 5) for i, v in enumerate(dbm)]

    marker = plan.freq_bin(433_920_000)
    cursor = plan.freq_bin(446_050_000)

    bursts = [lambda r: (r // 5) % 2 == 0, lambda r: True, lambda r: r % 2 == 0]
    hist = history(plan, carriers, bursts, seed=0x9C9C)

    dhz = plan.bin_freq(cursor) - plan.bin_freq(marker)
    ddb = dbm[cursor] - dbm[marker]

    draw_header(d, plan.bin_freq(cursor), dbm[cursor])
    draw_spectrum(d, plan, dbm, peak, cursor, SV_TOP, SV_SPLIT_AXIS, marker_bin=marker)
    draw_waterfall(d, hist, cursor, SV_SPLIT_WF_TOP, SV_SPLIT_WF_ROWS, marker_bin=marker)
    draw_strip(d, plan, None, delta=(dhz, ddb))
    save(img, "screen_marker.png")


def render_hint():
    img, d = canvas()
    plan = Plan(1)
    rng = LCG(0xA11CE)
    dbm = sweep(plan, CARRIERS_WIDE, rng)
    cursor = plan.freq_bin(433_920_000)
    strongest = max(range(AUR_BINS), key=lambda i: dbm[i])
    bursts = [lambda r: (r // 6) % 2 == 0, lambda r: True, lambda r: r % 3 != 0, lambda r: True]
    hist = history(plan, CARRIERS_WIDE, bursts)

    draw_header(d, plan.bin_freq(cursor), dbm[cursor])
    draw_spectrum(d, plan, dbm, None, cursor, SV_TOP, SV_SPLIT_AXIS)
    draw_waterfall(d, hist, cursor, SV_SPLIT_WF_TOP, SV_SPLIT_WF_ROWS)
    draw_hint(d)
    draw_strip(d, plan, (strongest, dbm[strongest]))
    save(img, "screen_hint.png")


def render_menu():
    img, d = canvas()
    text(d, 4, 11, "Aurora", f_pri)
    line(d, 0, 14, 127, 14)
    items = ["Scan the band", "Settings", "Controls", "About"]
    ROW_H = 12
    for i, it in enumerate(items):
        y = 15 + i * ROW_H
        col = FG
        if i == 0:
            box(d, 0, y, 124, ROW_H)
            col = BG
        text(d, 4, y + 9, it, f_sec, col)
    box(d, 125, 15, 3, 12)
    save(img, "screen_menu.png")


def render_settings():
    img, d = canvas()
    text(d, 4, 11, "Settings", f_pri)
    line(d, 0, 14, 127, 14)
    rows = [
        ("Band", "387-464", True),
        ("Detail", "Normal", False),
        ("Range", "50 dB", False),
        ("Peak trace", "Decay", False),
    ]
    ROW_H = 12
    for i, (k, v, sel) in enumerate(rows):
        y = 15 + i * ROW_H
        col = FG
        if sel:
            box(d, 0, y, 124, ROW_H)
            col = BG
        text(d, 4, y + 9, k, f_sec, col)
        text(d, 121, y + 9, v, f_sec, col, anchor="rs")
    box(d, 125, 15, 3, 12)
    save(img, "screen_settings.png")


NAMES = (
    "screen_split.png",
    "screen_waterfall.png",
    "screen_spectrum.png",
    "screen_zoom.png",
    "screen_marker.png",
    "screen_hold.png",
    "screen_hint.png",
    "screen_menu.png",
    "screen_settings.png",
)


def render_strip():
    imgs = [Image.open(os.path.join(OUT, n)) for n in NAMES]
    pad = 18
    strip = Image.new(
        "RGB",
        (sum(i.width for i in imgs) + pad * (len(imgs) + 1), imgs[0].height + pad * 2),
        (12, 14, 20),
    )
    x = pad
    for im in imgs:
        strip.paste(im, (x, pad))
        x += im.width + pad
    strip.save(os.path.join(OUT, "screens.png"))
    print("wrote", os.path.join(OUT, "screens.png"))


if __name__ == "__main__":
    render_split()
    render_waterfall()
    render_spectrum()
    render_zoom()
    render_marker()
    render_hold()
    render_hint()
    render_menu()
    render_settings()
    render_strip()
