/**
 * Aurora - the scanner's arithmetic, with no Flipper in it.
 *
 * Everything here is pure integer maths on plain types: which frequency a
 * screen column stands for, how far you may zoom before the radio (not the
 * plan) becomes the limit, where the noise floor sits in a sweep, and how a
 * dBm reading becomes pixels. It is split out precisely because these are the
 * things a scanner gets subtly wrong - an off-by-one in the bin mapping puts
 * every label half a channel out and nothing on screen looks broken - so they
 * are compiled for the host and unit-tested on every push. See test/.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Screen columns in a sweep. 64 bins x 2 px = the full 128 px width. */
#define AUR_BINS 64

/** Waterfall history the sweeper keeps. Matches the tallest waterfall drawn. */
#define AUR_WF_ROWS 40

/** Dither levels, 0 (floor) .. 16 (saturated). */
#define AUR_LEVELS 17

/** Reading used for a bin the firmware refuses to tune. */
#define AUR_DBM_INVALID ((int16_t)-127)

/**
 * Narrowest span offered. The CC1101's receive filter is 270-650 kHz wide, so
 * a carrier already smears across several bins at 2 MHz / 64. Zooming past
 * this would draw finer columns without resolving anything finer - a made-up
 * precision this app declines to imply.
 */
#define AUR_MIN_SPAN 2000000u

/** Hard ceiling on zoom steps, so a future wide band cannot run away. */
#define AUR_ZOOM_LIMIT 8

/** One contiguous range the Flipper firmware permits on Sub-GHz. */
typedef struct {
    uint32_t low; /* Hz, inclusive   */
    uint32_t high; /* Hz, inclusive   */
    const char* label; /* "387-464"       */
    const char* note; /* "433 ISM"       */
} AurBand;

#define AUR_BAND_COUNT 3
extern const AurBand aur_bands[AUR_BAND_COUNT];

/** What the sweeper walks and what the view labels. */
typedef struct {
    uint32_t center; /* Hz                          */
    uint32_t span; /* Hz, always <= band width    */
    uint8_t band; /* index into aur_bands        */
    uint8_t zoom; /* 0 = the whole band          */
} AurPlan;

/* ---- plan ---- */

/** Whole band, centred, zoom 0. */
void aur_plan_init(AurPlan* p, uint8_t band);

/** Deepest zoom this band allows before AUR_MIN_SPAN bites. */
uint8_t aur_zoom_max(uint8_t band);

/** Span in Hz at a zoom step (clamped to the band's own maximum zoom). */
uint32_t aur_span_for(uint8_t band, uint8_t zoom);

/**
 * Zoom by `delta` steps around `focus`, then slide the window back inside the
 * band. Zooming around the cursor rather than the centre is what lets you walk
 * a peak down from "the whole ISM band" to "that channel" without losing it.
 */
void aur_plan_zoom(AurPlan* p, int8_t delta, uint32_t focus);

/** Lowest frequency drawn (left edge of bin 0). */
uint32_t aur_plan_low(const AurPlan* p);

/* ---- bins ---- */

uint32_t aur_bin_width(const AurPlan* p);

/** Centre frequency of a bin - the number shown when the cursor sits on it. */
uint32_t aur_bin_freq(const AurPlan* p, uint8_t bin);

/** Bin a frequency lands in, saturating at the edges. */
uint8_t aur_freq_bin(const AurPlan* p, uint32_t freq);

/** Pull a cursor frequency back onto the drawn range. */
uint32_t aur_clamp_cursor(const AurPlan* p, uint32_t freq);

/** A round tick spacing giving roughly 4-8 gridlines across the span. */
uint32_t aur_tick_step(uint32_t span);

/**
 * Format a signed frequency difference for the marker readout: "+37k",
 * "-120k", "+1.23M". Picks kHz below a megahertz and MHz above, so a delta
 * stays legible whether the two points are one bin or half a band apart.
 * `buf` must be at least 16 bytes (a paranoid worst case, since the format
 * checker assumes %lu prints ten digits).
 */
void aur_fmt_delta_hz(char* buf, uint8_t len, int32_t hz);

/* ---- levels ---- */

/**
 * k-th smallest of n readings. Used with k around the lower quartile to read
 * the noise floor straight off a sweep: a handful of loud carriers cannot drag
 * a quartile the way they drag a mean.
 */
int16_t aur_percentile(const int16_t* vals, uint8_t n, uint8_t k);

/** dBm above the floor -> 0..AUR_LEVELS-1, for the waterfall's dither. */
uint8_t aur_level(int16_t dbm, int16_t floor_dbm, uint8_t range_db);

/** dBm above the floor -> 0..height px, for the spectrum's bars. */
uint8_t aur_scale_px(int16_t dbm, int16_t floor_dbm, uint8_t range_db, uint8_t height);

/**
 * dB of headroom the display keeps below the measured floor.
 *
 * Anchoring the scale exactly at the floor renders the noise as dead black,
 * which looks like a stopped radio. Dropping the zero point a few dB below it
 * leaves the noise shimmering at the bottom of the dither ramp - the texture
 * that tells you at a glance the sweep is still running. The top of the scale
 * does not move; the headroom is added to the range, not stolen from it.
 */
#define AUR_FLOOR_BIAS_DB 6

/** As aur_scale_px, but measured from the biased floor. Use this for drawing. */
uint8_t aur_display_px(int16_t dbm, int16_t floor_dbm, uint8_t range_db, uint8_t height);

/** As aur_level, but measured from the biased floor. Use this for drawing. */
uint8_t aur_display_level(int16_t dbm, int16_t floor_dbm, uint8_t range_db);

/** Ordered 4x4 Bayer test: is this pixel lit at this level? */
bool aur_dither(uint8_t level, uint8_t x, uint8_t y);

#ifdef __cplusplus
}
#endif
