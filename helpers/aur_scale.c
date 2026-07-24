#include "aur_scale.h"

#include <stdio.h> /* snprintf, for the marker delta readout */

/* The three ranges flipperzero-firmware will let a FAP tune. Staying inside
 * them is why the sweep never has to draw a "blocked" column in practice - but
 * the sweeper still checks each bin, because the firmware is the authority. */
const AurBand aur_bands[AUR_BAND_COUNT] = {
    {300000000u, 348000000u, "300-348", "315 ISM"},
    {387000000u, 464000000u, "387-464", "433 ISM"},
    {779000000u, 928000000u, "779-928", "868/915"},
};

static uint8_t band_clamp(uint8_t band) {
    return band < AUR_BAND_COUNT ? band : 0;
}

static uint32_t band_width(uint8_t band) {
    const AurBand* b = &aur_bands[band_clamp(band)];
    return b->high - b->low;
}

uint8_t aur_zoom_max(uint8_t band) {
    uint32_t w = band_width(band);
    uint8_t z = 0;
    while(z < AUR_ZOOM_LIMIT && (w >> (z + 1)) >= AUR_MIN_SPAN) z++;
    return z;
}

uint32_t aur_span_for(uint8_t band, uint8_t zoom) {
    uint8_t zm = aur_zoom_max(band);
    if(zoom > zm) zoom = zm;
    return band_width(band) >> zoom;
}

/* Slide the window until it sits wholly inside the band. At zoom 0 the two
 * bounds meet and the centre is pinned, which is exactly right.
 *
 * The two halves are deliberately not the same number. A span of 2328125 Hz
 * puts 1164062 below the centre and 1164063 above it, and clamping with the
 * rounded-down half on both sides walks the top edge one hertz outside the
 * band - which the firmware then refuses to tune. */
static void plan_clamp(AurPlan* p) {
    const AurBand* b = &aur_bands[band_clamp(p->band)];
    uint32_t half_lo = p->span / 2;
    uint32_t half_hi = p->span - half_lo;
    uint32_t lo = b->low + half_lo;
    uint32_t hi = b->high - half_hi;
    if(hi < lo) {
        p->center = b->low + (b->high - b->low) / 2;
        return;
    }
    if(p->center < lo) p->center = lo;
    if(p->center > hi) p->center = hi;
}

void aur_plan_init(AurPlan* p, uint8_t band) {
    band = band_clamp(band);
    p->band = band;
    p->zoom = 0;
    p->span = aur_span_for(band, 0);
    p->center = aur_bands[band].low + band_width(band) / 2;
    plan_clamp(p);
}

void aur_plan_zoom(AurPlan* p, int8_t delta, uint32_t focus) {
    int16_t z = (int16_t)p->zoom + delta;
    uint8_t zm = aur_zoom_max(p->band);
    if(z < 0) z = 0;
    if(z > (int16_t)zm) z = (int16_t)zm;

    p->zoom = (uint8_t)z;
    p->span = aur_span_for(p->band, p->zoom);
    p->center = focus;
    plan_clamp(p);
}

uint32_t aur_plan_low(const AurPlan* p) {
    return p->center - p->span / 2;
}

uint32_t aur_bin_width(const AurPlan* p) {
    uint32_t bw = p->span / AUR_BINS;
    return bw ? bw : 1;
}

uint32_t aur_bin_freq(const AurPlan* p, uint8_t bin) {
    if(bin >= AUR_BINS) bin = AUR_BINS - 1;
    uint32_t bw = aur_bin_width(p);
    return aur_plan_low(p) + bw * bin + bw / 2;
}

uint8_t aur_freq_bin(const AurPlan* p, uint32_t freq) {
    uint32_t lo = aur_plan_low(p);
    if(freq <= lo) return 0;
    uint32_t bin = (freq - lo) / aur_bin_width(p);
    return bin >= AUR_BINS ? (uint8_t)(AUR_BINS - 1) : (uint8_t)bin;
}

uint32_t aur_clamp_cursor(const AurPlan* p, uint32_t freq) {
    uint32_t lo = aur_bin_freq(p, 0);
    uint32_t hi = aur_bin_freq(p, AUR_BINS - 1);
    if(freq < lo) return lo;
    if(freq > hi) return hi;
    return freq;
}

uint32_t aur_tick_step(uint32_t span) {
    /* 1-2-5 decades, in Hz. The first one that keeps the grid under 8 ticks
     * wins, so the axis stays legible from 149 MHz down to 2 MHz. */
    static const uint32_t steps[] = {
        100000u,
        200000u,
        500000u,
        1000000u,
        2000000u,
        5000000u,
        10000000u,
        20000000u,
        50000000u,
        100000000u,
    };
    const uint8_t n = (uint8_t)(sizeof(steps) / sizeof(steps[0]));
    for(uint8_t i = 0; i < n; i++) {
        if(span / steps[i] <= 8) return steps[i];
    }
    return steps[n - 1];
}

void aur_fmt_delta_hz(char* buf, uint8_t len, int32_t hz) {
    const char* sign = hz < 0 ? "-" : "+";
    /* Fold to unsigned before negating, so INT32_MIN cannot overflow. Real
     * deltas are bounded by a band width (< 150 MHz), but the guard is free. */
    uint32_t mag = hz < 0 ? (uint32_t)(-(int64_t)hz) : (uint32_t)hz;

    /* Round to the nearest kHz first, then decide the unit off the rounded
     * value - otherwise 999.6 kHz would print as "1000k" instead of "1.00M". */
    uint32_t khz = (mag + 500u) / 1000u;
    if(khz >= 1000u) {
        uint32_t mhz = khz / 1000u;
        uint32_t frac = (khz % 1000u) / 10u; /* two decimals */
        snprintf(buf, len, "%s%lu.%02luM", sign, (unsigned long)mhz, (unsigned long)frac);
    } else {
        snprintf(buf, len, "%s%luk", sign, (unsigned long)khz);
    }
}

int16_t aur_percentile(const int16_t* vals, uint8_t n, uint8_t k) {
    if(!vals || n == 0) return 0;
    if(k >= n) k = (uint8_t)(n - 1);

    int16_t buf[AUR_BINS];
    if(n > AUR_BINS) n = AUR_BINS;
    if(k >= n) k = (uint8_t)(n - 1);
    for(uint8_t i = 0; i < n; i++)
        buf[i] = vals[i];

    /* Partial selection sort: only the first k+1 positions need to be right. */
    for(uint8_t i = 0; i <= k; i++) {
        uint8_t m = i;
        for(uint8_t j = (uint8_t)(i + 1); j < n; j++) {
            if(buf[j] < buf[m]) m = j;
        }
        int16_t t = buf[i];
        buf[i] = buf[m];
        buf[m] = t;
    }
    return buf[k];
}

uint8_t aur_level(int16_t dbm, int16_t floor_dbm, uint8_t range_db) {
    return aur_scale_px(dbm, floor_dbm, range_db, AUR_LEVELS - 1);
}

uint8_t aur_scale_px(int16_t dbm, int16_t floor_dbm, uint8_t range_db, uint8_t height) {
    if(height == 0) return 0;
    if(range_db == 0) range_db = 1;
    if(dbm <= floor_dbm) return 0;

    int32_t d = (int32_t)dbm - (int32_t)floor_dbm;
    if(d >= (int32_t)range_db) return height;
    return (uint8_t)((d * (int32_t)height) / (int32_t)range_db);
}

uint8_t aur_display_px(int16_t dbm, int16_t floor_dbm, uint8_t range_db, uint8_t height) {
    /* Drop the zero point, then give the range the same amount back, so the
     * top of the scale still lands exactly at floor + range_db. */
    return aur_scale_px(
        dbm,
        (int16_t)(floor_dbm - AUR_FLOOR_BIAS_DB),
        (uint8_t)(range_db + AUR_FLOOR_BIAS_DB),
        height);
}

uint8_t aur_display_level(int16_t dbm, int16_t floor_dbm, uint8_t range_db) {
    return aur_display_px(dbm, floor_dbm, range_db, AUR_LEVELS - 1);
}

bool aur_dither(uint8_t level, uint8_t x, uint8_t y) {
    /* Classic 4x4 ordered dither. On a 1-bit screen this is what turns a
     * scalar into apparent brightness, and it is why the waterfall reads as
     * shades of grey instead of a black-and-white stipple. */
    static const uint8_t bayer4[16] = {
        0,
        8,
        2,
        10,
        12,
        4,
        14,
        6,
        3,
        11,
        1,
        9,
        15,
        7,
        13,
        5,
    };
    return level > bayer4[((y & 3u) << 2) | (x & 3u)];
}
