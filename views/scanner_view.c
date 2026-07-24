#include "scanner_view.h"

#include <furi.h>
#include <gui/gui.h>
#include <stdio.h>
#include <string.h>

/* ---------------- layout ----------------
 * One set of constants for the C and for tools_gen_mockups.py, which mirrors
 * this file line for line so a collision shows up in the README before it
 * ships on a device.
 */
#define SV_W 128
#define SV_H 64

#define SV_HDR_BASE 9 /* header text baseline          */
#define SV_RULE_Y 11 /* hairline under the header     */
#define SV_TOP 13 /* first row of content          */

#define SV_STRIP_Y 53 /* inverted status strip         */
#define SV_STRIP_H 11
#define SV_STRIP_BASE 62

#define SV_SPLIT_AXIS 31 /* spectrum baseline in split    */
#define SV_SPLIT_WF_TOP 33
#define SV_SPLIT_WF_ROWS 20

#define SV_WF_TOP SV_TOP
#define SV_WF_ROWS 40

/* The activity map sits under the header rule as its own 2 px band. Hanging it
 * off the frequency axis instead reads as a second axis - two dotted rows two
 * pixels apart merge into one thick smear at this size. */
#define SV_SPECT_HITS 12
#define SV_SPECT_TOP 16
#define SV_SPECT_AXIS 50 /* two clear rows above the strip, so the cursor
                          * caret at axis+1 cannot land inside it */

struct ScannerView {
    View* view;
    ScannerViewEventCallback cb;
    void* ctx;
};

typedef struct {
    AurSweepSnapshot sn;
    ScannerUi ui;
} ScannerModel;

/* ---------------- formatting ---------------- */

/** 433920000 -> "433.920". Buffers are sized for the worst case, not the
 * plausible one: -Werror=format-truncation assumes %lu is ten digits. */
static void fmt_mhz3(char* buf, size_t len, uint32_t hz) {
    snprintf(buf, len, "%lu.%03lu", (unsigned long)(hz / 1000000u), (unsigned long)((hz % 1000000u) / 1000u));
}

/** 433920000 -> "433.92" - the strip has less room than the header. */
static void fmt_mhz2(char* buf, size_t len, uint32_t hz) {
    snprintf(buf, len, "%lu.%02lu", (unsigned long)(hz / 1000000u), (unsigned long)((hz % 1000000u) / 10000u));
}

/** 19250000 -> "19.2M". */
static void fmt_span(char* buf, size_t len, uint32_t hz) {
    snprintf(buf, len, "%lu.%luM", (unsigned long)(hz / 1000000u), (unsigned long)((hz % 1000000u) / 100000u));
}

/* ---------------- small drawing helpers ---------------- */

static void draw_dotted_v(Canvas* c, int x, int y0, int y1) {
    for(int y = y0; y <= y1; y += 2)
        canvas_draw_dot(c, x, y);
}

/** A solid triangle pointing up, apex at (x, y), four rows tall. */
static void draw_caret_up(Canvas* c, int x, int y) {
    for(int i = 0; i < 4; i++)
        canvas_draw_line(c, x - i, y + i, x + i, y + i);
}

/**
 * The measurement marker. A coarser, two-tone dashed guide than the cursor's
 * fine dotted one, capped with a little tab at the top - so the two are never
 * confused, and the marker stays visible over both bright signal and dark
 * noise in the waterfall. `mx` is the marker's left pixel column.
 */
static void draw_marker(Canvas* c, int mx, int top, int bottom) {
    /* top tab: a white block with a black outline, readable on any background */
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, mx - 1, top, 4, 3);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, mx - 1, top, 4, 3);

    for(int y = top + 3; y <= bottom; y += 4) {
        canvas_set_color(c, ColorWhite);
        canvas_draw_dot(c, mx, y);
        if(y + 1 <= bottom) canvas_draw_dot(c, mx, y + 1);
        canvas_set_color(c, ColorBlack);
        canvas_draw_dot(c, mx + 1, y);
        if(y + 1 <= bottom) canvas_draw_dot(c, mx + 1, y + 1);
    }
    canvas_set_color(c, ColorBlack);
}

/* ---------------- faces ---------------- */

static void draw_header(Canvas* c, const ScannerModel* m) {
    const AurSweepSnapshot* sn = &m->sn;
    char buf[20];

    fmt_mhz3(buf, sizeof(buf), m->ui.cursor_freq);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, SV_HDR_BASE, buf);

    canvas_set_font(c, FontSecondary);
    int16_t v = sn->dbm[m->ui.cursor_bin < AUR_BINS ? m->ui.cursor_bin : 0];
    if(v == AUR_DBM_INVALID)
        snprintf(buf, sizeof(buf), "-- dBm");
    else
        snprintf(buf, sizeof(buf), "%d dBm", (int)v);
    canvas_draw_str_aligned(c, 126, SV_HDR_BASE, AlignRight, AlignBottom, buf);

    canvas_draw_line(c, 0, SV_RULE_Y, 127, SV_RULE_Y);
}

static void draw_strip(Canvas* c, const ScannerModel* m) {
    const AurSweepSnapshot* sn = &m->sn;
    char buf[24];

    canvas_draw_box(c, 0, SV_STRIP_Y, SV_W, SV_STRIP_H);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);

    if(m->ui.hold) {
        /* A held display is a lie in progress unless it says so loudly, so
         * HOLD gets the one inverted pill on the screen. */
        canvas_draw_rbox(c, 2, 54, 27, 9, 2);
        canvas_set_color(c, ColorBlack);
        canvas_draw_str(c, 6, 61, "HOLD");
        canvas_set_color(c, ColorWhite);
    } else {
        fmt_span(buf, sizeof(buf), sn->plan.span);
        canvas_draw_str(c, 3, SV_STRIP_BASE, buf);
    }

    if(m->ui.marker_set) {
        /* A marker means you care about the delta now, so it takes the strip's
         * right slot from the strongest-bin readout: a tab icon echoing the
         * on-plot flag, then Δf, then ΔdB when both points are on-window this
         * sweep. (A drawn tab, not a 'Δ' glyph the built-in font may not have.) */
        char dfreq[16];
        aur_fmt_delta_hz(dfreq, sizeof(dfreq), m->ui.delta_hz);
        if(m->ui.delta_db_valid)
            snprintf(buf, sizeof(buf), "%s %+d", dfreq, (int)m->ui.delta_db);
        else
            snprintf(buf, sizeof(buf), "%s", dfreq);
        canvas_draw_str_aligned(c, 125, SV_STRIP_BASE, AlignRight, AlignBottom, buf);
        int w = canvas_string_width(c, buf);
        int tabx = 125 - w - 6;
        canvas_draw_box(c, tabx, 55, 4, 4); // white on the inverted strip
        canvas_set_color(c, ColorBlack);
        canvas_draw_frame(c, tabx, 55, 4, 4);
        canvas_set_color(c, ColorWhite);
    } else if(sn->strongest_dbm != AUR_DBM_INVALID) {
        uint32_t pf = aur_bin_freq(&sn->plan, sn->strongest_bin);
        char fbuf[12];
        fmt_mhz2(fbuf, sizeof(fbuf), pf);
        snprintf(buf, sizeof(buf), "%s %d", fbuf, (int)sn->strongest_dbm);
        canvas_draw_str_aligned(c, 125, SV_STRIP_BASE, AlignRight, AlignBottom, buf);
        int w = canvas_string_width(c, buf);
        draw_caret_up(c, 125 - w - 5, 56);
    }

    canvas_set_color(c, ColorBlack);
}

static void draw_axis(Canvas* c, const AurPlan* p, int axis) {
    for(int x = 0; x < SV_W; x += 2)
        canvas_draw_dot(c, x, axis);

    /* Round-frequency ticks. Everything stays in 32 bits: dividing the offset
     * by Hz-per-pixel avoids the (offset * 128) that would overflow. */
    uint32_t step = aur_tick_step(p->span);
    uint32_t lo = aur_plan_low(p);
    uint32_t hz_per_px = p->span / SV_W;
    if(hz_per_px == 0) hz_per_px = 1;

    uint32_t first = ((lo + step - 1) / step) * step;
    for(uint32_t f = first; f <= lo + p->span; f += step) {
        uint32_t x = (f - lo) / hz_per_px;
        if(x < SV_W) canvas_draw_line(c, (int)x, axis - 1, (int)x, axis + 1);
    }
}

static void draw_spectrum(Canvas* c, const ScannerModel* m, int top, int axis) {
    const AurSweepSnapshot* sn = &m->sn;
    const uint8_t height = (uint8_t)(axis - top);
    const uint8_t range = m->ui.range_db;

    for(uint8_t i = 0; i < AUR_BINS; i++) {
        int x = i * 2;
        int16_t v = sn->dbm[i];

        if(v == AUR_DBM_INVALID) {
            /* A bin the firmware will not tune. Hatched, so it can never be
             * mistaken for a genuinely quiet channel. */
            for(int y = top; y < axis; y += 3)
                canvas_draw_dot(c, x + ((y - top) & 1), y);
            continue;
        }

        uint8_t h = aur_display_px(v, sn->floor_dbm, range, height);
        if(h > 0) canvas_draw_box(c, x, axis - h, 2, h);

        int16_t pv = sn->peak_dbm[i];
        if(pv != AUR_DBM_INVALID) {
            uint8_t ph = aur_display_px(pv, sn->floor_dbm, range, height);
            if(ph > h + 1) canvas_draw_box(c, x, axis - ph, 2, 1);
        }
    }

    draw_axis(c, &sn->plan, axis);

    if(m->ui.marker_set && m->ui.marker_on) draw_marker(c, m->ui.marker_bin * 2, top, axis - 1);

    /* Cursor: dotted down the trace, solid where it meets the axis. */
    int cx = m->ui.cursor_bin * 2;
    draw_dotted_v(c, cx, top, axis - 2);
    canvas_draw_box(c, cx, axis - 1, 2, 3);
}

static void draw_waterfall(Canvas* c, const ScannerModel* m, int top, int rows, bool time_ticks) {
    const AurSweepSnapshot* sn = &m->sn;
    int drawn = rows < (int)sn->wf_count ? rows : (int)sn->wf_count;

    for(int r = 0; r < drawn; r++) {
        const uint8_t* src = sn->wf[(sn->wf_head + AUR_WF_ROWS - r) % AUR_WF_ROWS];
        int y = top + r;
        for(uint8_t i = 0; i < AUR_BINS; i++) {
            uint8_t lvl = src[i];
            if(!lvl) continue;
            int x = i * 2;
            if(aur_dither(lvl, (uint8_t)x, (uint8_t)y)) canvas_draw_dot(c, x, y);
            if(aur_dither(lvl, (uint8_t)(x + 1), (uint8_t)y)) canvas_draw_dot(c, x + 1, y);
        }
    }

    if(m->ui.marker_set && m->ui.marker_on)
        draw_marker(c, m->ui.marker_bin * 2, top, top + rows - 1);

    /* Cursor guide, drawn as a two-tone barber pole: a white column beside a
     * black one, so it stays visible over bright signal and dark noise alike. */
    int cx = m->ui.cursor_bin * 2;
    canvas_set_color(c, ColorWhite);
    for(int y = top; y < top + rows; y += 2)
        canvas_draw_dot(c, cx, y);
    canvas_set_color(c, ColorBlack);
    for(int y = top + 1; y < top + rows; y += 2)
        canvas_draw_dot(c, cx + 1, y);

    if(!time_ticks) return;

    /* One notch per elapsed second, from the measured sweep rate rather than
     * an assumed one - so a Fine sweep's waterfall is still honest about time. */
    uint16_t rate10 = sn->sweeps_x10_per_sec;
    if(rate10 < 5) return;
    for(uint32_t s = 1;; s++) {
        int y = top + (int)((s * rate10) / 10u);
        if(y >= top + rows) break;
        canvas_set_color(c, ColorWhite);
        canvas_draw_line(c, 123, y, 127, y);
        canvas_set_color(c, ColorBlack);
        canvas_draw_line(c, 126, y, 127, y);
    }
}

static void draw_hits(Canvas* c, const ScannerModel* m, int y) {
    for(uint8_t i = 0; i < AUR_BINS; i++) {
        if(m->sn.hit[i]) canvas_draw_box(c, i * 2, y, 2, 2);
    }
}

static void draw_hint(Canvas* c) {
    /* Transient primer only - the Controls page carries the full reference,
     * so this shows the four gestures a first run most needs and keeps every
     * line inside the ~22 characters FontSecondary fits across the box. */
    canvas_set_color(c, ColorWhite);
    canvas_draw_rbox(c, 3, 26, 122, 26, 3);
    canvas_set_color(c, ColorBlack);
    canvas_draw_rframe(c, 3, 26, 122, 26, 3);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 33, AlignCenter, AlignCenter, "<> cursor   ^v zoom");
    canvas_draw_str_aligned(c, 64, 41, AlignCenter, AlignCenter, "OK view  hold OK snap");
    canvas_draw_str_aligned(c, 64, 49, AlignCenter, AlignCenter, "hold ^ = drop marker");
}

static void draw_waiting(Canvas* c) {
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 32, AlignCenter, AlignCenter, "sweeping...");
}

static void draw_error(Canvas* c) {
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 28, AlignCenter, AlignCenter, "Sub-GHz busy");
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 64, 42, AlignCenter, AlignCenter, "close other radio apps");
}

static void scanner_view_draw(Canvas* c, void* model) {
    ScannerModel* m = model;

    draw_header(c, m);

    /* `running` and `valid` are published together the moment the worker has
     * had a look at the radio. Testing `valid` alone would flash "Sub-GHz
     * busy" during the first frames, before anyone has asked the radio
     * anything - a zeroed snapshot is not a failed one. */
    if(m->sn.running && !m->sn.valid) {
        draw_error(c);
        draw_strip(c, m);
        return;
    }

    if(m->sn.wf_count == 0) {
        draw_waiting(c);
        draw_strip(c, m);
        return;
    }

    switch(m->ui.mode) {
    case AurModeWaterfall:
        draw_waterfall(c, m, SV_WF_TOP, SV_WF_ROWS, true);
        break;
    case AurModeSpectrum:
        draw_hits(c, m, SV_SPECT_HITS);
        draw_spectrum(c, m, SV_SPECT_TOP, SV_SPECT_AXIS);
        break;
    default:
        draw_spectrum(c, m, SV_TOP, SV_SPLIT_AXIS);
        draw_waterfall(c, m, SV_SPLIT_WF_TOP, SV_SPLIT_WF_ROWS, false);
        break;
    }

    if(m->ui.show_hint) draw_hint(c);
    draw_strip(c, m);
}

/* ---------------- input ---------------- */

static void emit(ScannerView* v, ScannerViewEvent e) {
    if(v->cb) v->cb(v->ctx, e);
}

static bool scanner_view_input(InputEvent* event, void* context) {
    ScannerView* v = context;
    bool press = (event->type == InputTypeShort);
    bool repeat = (event->type == InputTypeRepeat);

    if(press || repeat) {
        switch(event->key) {
        case InputKeyLeft:
            emit(v, repeat ? ScannerEventCursorLeftFast : ScannerEventCursorLeft);
            return true;
        case InputKeyRight:
            emit(v, repeat ? ScannerEventCursorRightFast : ScannerEventCursorRight);
            return true;
        /* Zoom on the press only, never on repeat. Each step retunes the radio
         * and drops the traces, so a held key would strobe through every zoom
         * level and throw the history away several times over. */
        case InputKeyUp:
            if(press) emit(v, ScannerEventZoomIn);
            return true;
        case InputKeyDown:
            if(press) emit(v, ScannerEventZoomOut);
            return true;
        case InputKeyOk:
            if(press) {
                emit(v, ScannerEventCycleMode);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    if(event->type == InputTypeLong) {
        if(event->key == InputKeyOk) {
            emit(v, ScannerEventSnapPeak);
            return true;
        }
        /* Long-up drops or clears the marker. Up's short press zooms and its
         * repeat is ignored, so the long press is free and unambiguous. */
        if(event->key == InputKeyUp) {
            emit(v, ScannerEventToggleMarker);
            return true;
        }
        /* Long-back freezes. Consuming only the Long leaves a short Back to
         * exit as it does everywhere else on the device. */
        if(event->key == InputKeyBack) {
            emit(v, ScannerEventToggleHold);
            return true;
        }
    }
    return false;
}

/* ---------------- lifecycle ---------------- */

ScannerView* scanner_view_alloc(void) {
    ScannerView* v = malloc(sizeof(ScannerView));
    v->cb = NULL;
    v->ctx = NULL;
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, scanner_view_draw);
    view_set_input_callback(v->view, scanner_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ScannerModel));
    with_view_model(v->view, ScannerModel * m, { memset(m, 0, sizeof(ScannerModel)); }, false);
    return v;
}

void scanner_view_free(ScannerView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* scanner_view_get_view(ScannerView* v) {
    furi_assert(v);
    return v->view;
}

void scanner_view_set_event_callback(ScannerView* v, ScannerViewEventCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->ctx = context;
}

void scanner_view_update(ScannerView* v, const AurSweepSnapshot* sn, const ScannerUi* ui) {
    furi_assert(v);
    with_view_model(
        v->view,
        ScannerModel * m,
        {
            m->sn = *sn;
            m->ui = *ui;
        },
        true);
}
