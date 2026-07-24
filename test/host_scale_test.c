/*
 * Host tests for helpers/aur_scale.c - the scanner's arithmetic.
 *
 * These are the failures a screenshot cannot catch. If a bin maps to the wrong
 * frequency, every label on the display is confidently, quietly wrong; if the
 * zoom clamp is off by one, the window slides outside a band the firmware will
 * tune and half the sweep goes dead. So the mapping is asserted round-trip
 * across every band and every zoom step, not spot-checked.
 *
 *   make -C test
 */
#include "helpers/aur_scale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        checks++;                                           \
        if(!(cond)) {                                       \
            failures++;                                     \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while(0)

static void section(const char* name) {
    printf("\n== %s\n", name);
}

/* ---------------------------------------------------------------- bands */

static void test_bands(void) {
    section("bands sit inside what the firmware will tune");
    for(uint8_t b = 0; b < AUR_BAND_COUNT; b++) {
        CHECK(aur_bands[b].low < aur_bands[b].high, "band %u inverted", b);
        CHECK(
            aur_bands[b].high - aur_bands[b].low >= AUR_MIN_SPAN,
            "band %u narrower than the minimum span",
            b);
        CHECK(aur_bands[b].label != NULL && aur_bands[b].note != NULL, "band %u unlabelled", b);
    }
}

/* ----------------------------------------------------------------- zoom */

static void test_zoom_limits(void) {
    section("zoom stops before the span becomes a lie");
    for(uint8_t b = 0; b < AUR_BAND_COUNT; b++) {
        uint8_t zm = aur_zoom_max(b);
        CHECK(zm >= 1, "band %u offers no zoom at all", b);
        CHECK(zm <= AUR_ZOOM_LIMIT, "band %u zoom %u past the hard ceiling", b, zm);

        CHECK(
            aur_span_for(b, zm) >= AUR_MIN_SPAN,
            "band %u deepest zoom spans %u Hz, under the floor",
            b,
            aur_span_for(b, zm));
        CHECK(
            aur_span_for(b, (uint8_t)(zm + 1)) == aur_span_for(b, zm),
            "band %u zoom past max was not clamped",
            b);
        CHECK(aur_span_for(b, 0) == aur_bands[b].high - aur_bands[b].low, "band %u zoom 0 is not the whole band", b);

        for(uint8_t z = 1; z <= zm; z++) {
            CHECK(
                aur_span_for(b, z) < aur_span_for(b, (uint8_t)(z - 1)),
                "band %u zoom %u did not narrow",
                b,
                z);
        }
    }
}

static void test_window_stays_in_band(void) {
    section("the window never leaves the band, however you shove it");
    for(uint8_t b = 0; b < AUR_BAND_COUNT; b++) {
        const AurBand* band = &aur_bands[b];
        uint8_t zm = aur_zoom_max(b);

        /* Aim the focus well outside the band in both directions and at both
         * edges - a user holding Left at the bottom of a zoomed window does
         * exactly this, several times a second. */
        const uint32_t focuses[] = {
            0u,
            band->low - 1u,
            band->low,
            band->low + 1u,
            band->low + (band->high - band->low) / 2u,
            band->high - 1u,
            band->high,
            band->high + 50000000u,
            0xFFFFFFFFu,
        };

        for(uint8_t z = 0; z <= zm; z++) {
            for(size_t i = 0; i < sizeof(focuses) / sizeof(focuses[0]); i++) {
                AurPlan p;
                aur_plan_init(&p, b);
                aur_plan_zoom(&p, (int8_t)z, focuses[i]);

                uint32_t lo = aur_plan_low(&p);
                uint32_t hi = lo + p.span;
                CHECK(
                    lo >= band->low,
                    "band %u zoom %u focus %u: low edge %u below %u",
                    b,
                    z,
                    focuses[i],
                    lo,
                    band->low);
                CHECK(
                    hi <= band->high,
                    "band %u zoom %u focus %u: high edge %u above %u",
                    b,
                    z,
                    focuses[i],
                    hi,
                    band->high);
            }
        }
    }
}

static void test_zoom_keeps_the_focus(void) {
    section("zooming in keeps the thing you were pointing at");
    /* The whole point of zooming around the cursor: a peak you have parked on
     * must survive every step down, not slide off the edge. */
    for(uint8_t b = 0; b < AUR_BAND_COUNT; b++) {
        const AurBand* band = &aur_bands[b];
        uint8_t zm = aur_zoom_max(b);

        uint32_t targets[3] = {
            band->low + (band->high - band->low) / 4u,
            band->low + (band->high - band->low) / 2u,
            band->high - (band->high - band->low) / 4u,
        };

        for(size_t t = 0; t < 3; t++) {
            AurPlan p;
            aur_plan_init(&p, b);
            for(uint8_t z = 1; z <= zm; z++) {
                aur_plan_zoom(&p, +1, targets[t]);
                uint32_t lo = aur_plan_low(&p);
                CHECK(
                    targets[t] >= lo && targets[t] <= lo + p.span,
                    "band %u zoom %u lost target %u (window %u..%u)",
                    b,
                    z,
                    targets[t],
                    lo,
                    lo + p.span);
            }
        }
    }
}

/* ----------------------------------------------------------------- bins */

static void test_bin_roundtrip(void) {
    section("bin <-> frequency round-trips at every zoom in every band");
    for(uint8_t b = 0; b < AUR_BAND_COUNT; b++) {
        uint8_t zm = aur_zoom_max(b);
        for(uint8_t z = 0; z <= zm; z++) {
            AurPlan p;
            aur_plan_init(&p, b);
            aur_plan_zoom(&p, (int8_t)z, aur_bands[b].low + (aur_bands[b].high - aur_bands[b].low) / 2u);

            for(uint8_t bin = 0; bin < AUR_BINS; bin++) {
                uint32_t f = aur_bin_freq(&p, bin);
                uint8_t back = aur_freq_bin(&p, f);
                CHECK(back == bin, "band %u zoom %u bin %u -> %u Hz -> bin %u", b, z, bin, f, back);
            }

            /* Bins tile the window in order, with no gap and no overlap. */
            for(uint8_t bin = 1; bin < AUR_BINS; bin++) {
                CHECK(
                    aur_bin_freq(&p, bin) > aur_bin_freq(&p, (uint8_t)(bin - 1)),
                    "band %u zoom %u bins %u/%u out of order",
                    b,
                    z,
                    bin - 1,
                    bin);
            }
        }
    }
}

static void test_bin_saturates(void) {
    section("out-of-window frequencies saturate rather than wrap");
    AurPlan p;
    aur_plan_init(&p, 1);
    aur_plan_zoom(&p, +2, 433920000u);

    uint32_t lo = aur_plan_low(&p);
    CHECK(aur_freq_bin(&p, 0u) == 0, "zero did not saturate low");
    CHECK(aur_freq_bin(&p, lo - 1000000u) == 0, "below-window did not saturate low");
    CHECK(aur_freq_bin(&p, 0xFFFFFFFFu) == AUR_BINS - 1, "huge did not saturate high");
    CHECK(aur_freq_bin(&p, lo + p.span + 1000000u) == AUR_BINS - 1, "above-window did not saturate high");

    CHECK(aur_bin_freq(&p, AUR_BINS) == aur_bin_freq(&p, AUR_BINS - 1), "bin overflow not clamped");

    CHECK(aur_clamp_cursor(&p, 0u) == aur_bin_freq(&p, 0), "cursor clamp low");
    CHECK(
        aur_clamp_cursor(&p, 0xFFFFFFFFu) == aur_bin_freq(&p, AUR_BINS - 1), "cursor clamp high");
    uint32_t inside = aur_bin_freq(&p, 20);
    CHECK(aur_clamp_cursor(&p, inside) == inside, "cursor clamp moved an in-window frequency");
}

static void test_tick_step(void) {
    section("gridlines stay countable at every span");
    for(uint8_t b = 0; b < AUR_BAND_COUNT; b++) {
        uint8_t zm = aur_zoom_max(b);
        for(uint8_t z = 0; z <= zm; z++) {
            uint32_t span = aur_span_for(b, z);
            uint32_t step = aur_tick_step(span);
            CHECK(step > 0, "zero tick step for span %u", span);
            CHECK(span / step <= 8, "span %u / step %u gives too many ticks", span, step);
            CHECK(span / step >= 1, "span %u / step %u gives no ticks", span, step);
        }
    }
}

/* --------------------------------------------------------------- levels */

static void test_percentile(void) {
    section("the noise floor is a quartile, not an average");
    int16_t flat[AUR_BINS];
    for(uint8_t i = 0; i < AUR_BINS; i++)
        flat[i] = -96;
    CHECK(aur_percentile(flat, AUR_BINS, AUR_BINS / 4) == -96, "flat sweep");

    /* Six loud carriers on an otherwise quiet band must not lift the floor.
     * A mean would be dragged up by more than 6 dB here; a quartile is not
     * moved at all, which is the entire reason for the choice. */
    int16_t busy[AUR_BINS];
    for(uint8_t i = 0; i < AUR_BINS; i++)
        busy[i] = -96;
    for(uint8_t i = 0; i < 6; i++)
        busy[i * 7] = -40;
    CHECK(aur_percentile(busy, AUR_BINS, AUR_BINS / 4) == -96, "carriers dragged the floor");

    int16_t ramp[8] = {-10, -20, -30, -40, -50, -60, -70, -80};
    CHECK(aur_percentile(ramp, 8, 0) == -80, "k=0 is the minimum");
    CHECK(aur_percentile(ramp, 8, 7) == -10, "k=n-1 is the maximum");
    CHECK(aur_percentile(ramp, 8, 1) == -70, "k=1 is the second smallest");

    CHECK(aur_percentile(ramp, 8, 99) == -10, "k past the end clamps to the maximum");
    CHECK(aur_percentile(NULL, 8, 0) == 0, "null input");
    CHECK(aur_percentile(ramp, 0, 0) == 0, "empty input");
}

static void test_scaling(void) {
    section("dBm maps onto pixels without lying at the ends");
    const int16_t floor = -96;

    CHECK(aur_scale_px(-96, floor, 50, 20) == 0, "at the floor is empty");
    CHECK(aur_scale_px(-120, floor, 50, 20) == 0, "below the floor is empty");
    CHECK(aur_scale_px(-46, floor, 50, 20) == 20, "at the top of the range is full");
    CHECK(aur_scale_px(-10, floor, 50, 20) == 20, "above the range stays full, never overflows");
    CHECK(aur_scale_px(-71, floor, 50, 20) == 10, "midpoint is half");

    CHECK(aur_scale_px(-40, floor, 0, 20) == 20, "zero range does not divide by zero");
    CHECK(aur_scale_px(-40, floor, 50, 0) == 0, "zero height stays zero");
    CHECK(aur_scale_px(AUR_DBM_INVALID, floor, 50, 20) == 0, "an untunable bin reads empty");

    /* Monotonic: a stronger signal never draws a shorter bar. */
    for(int16_t d = -120; d < -20; d++) {
        uint8_t a = aur_scale_px(d, floor, 50, 40);
        uint8_t bb = aur_scale_px((int16_t)(d + 1), floor, 50, 40);
        CHECK(bb >= a, "not monotonic at %d dBm (%u then %u)", d, a, bb);
        CHECK(a <= 40, "overflowed the height at %d dBm", d);
    }

    CHECK(aur_level(-96, floor, 50) == 0, "level at the floor");
    CHECK(aur_level(-40, floor, 50) == AUR_LEVELS - 1, "level saturates");
    for(int16_t d = -120; d < -20; d++)
        CHECK(aur_level(d, floor, 50) < AUR_LEVELS, "level out of range at %d dBm", d);

    section("the display keeps the noise alive without moving the top of scale");
    /* Noise sitting on the floor must land on the dither ramp, not on zero -
     * a waterfall that goes dead black looks like a stopped radio. */
    CHECK(aur_display_level(floor, floor, 50) > 0, "the floor renders as dead black");
    CHECK(aur_display_level(floor, floor, 50) <= 2, "the floor renders too bright");
    CHECK(
        aur_display_level((int16_t)(floor - AUR_FLOOR_BIAS_DB), floor, 50) == 0,
        "nothing is below the biased zero point");

    /* The headroom is added to the range, not taken out of it: a signal at the
     * top of the configured range must still peg the display exactly. */
    for(uint8_t r = 30; r <= 70; r += 20) {
        CHECK(
            aur_display_px((int16_t)(floor + r), floor, r, 40) == 40,
            "range %u: top of scale did not peg",
            r);
        CHECK(
            aur_display_px((int16_t)(floor + r - 1), floor, r, 40) < 40,
            "range %u: pegged one dB early",
            r);
    }

    for(int16_t d = -120; d < -20; d++) {
        CHECK(aur_display_level(d, floor, 70) < AUR_LEVELS, "display level out of range at %d", d);
        CHECK(aur_display_px(d, floor, 70, 40) <= 40, "display px overflowed at %d", d);
    }
}

static void test_dither(void) {
    section("the dither is an even ramp, not a stipple");
    /* Level 0 must be genuinely blank and the top level genuinely solid,
     * otherwise the waterfall either never goes dark or never goes bright. */
    int lit0 = 0, littop = 0;
    for(uint8_t y = 0; y < 4; y++) {
        for(uint8_t x = 0; x < 4; x++) {
            if(aur_dither(0, x, y)) lit0++;
            if(aur_dither(AUR_LEVELS - 1, x, y)) littop++;
        }
    }
    CHECK(lit0 == 0, "level 0 lit %d of 16 pixels", lit0);
    CHECK(littop == 16, "top level lit only %d of 16 pixels", littop);

    /* Density must rise by exactly one pixel per level across the 4x4 cell. */
    int prev = -1;
    for(uint8_t lvl = 0; lvl < AUR_LEVELS; lvl++) {
        int lit = 0;
        for(uint8_t y = 0; y < 4; y++)
            for(uint8_t x = 0; x < 4; x++)
                if(aur_dither(lvl, x, y)) lit++;
        CHECK(lit == (int)lvl, "level %u lit %d pixels, expected %u", lvl, lit, lvl);
        CHECK(lit > prev, "level %u did not get brighter", lvl);
        prev = lit;
    }
}

static void test_delta_format(void) {
    section("the marker delta reads right on both sides of zero");
    char b[16];

    aur_fmt_delta_hz(b, sizeof(b), 0);
    CHECK(strcmp(b, "+0k") == 0, "zero was '%s'", b);

    aur_fmt_delta_hz(b, sizeof(b), 37000);
    CHECK(strcmp(b, "+37k") == 0, "+37 kHz was '%s'", b);

    aur_fmt_delta_hz(b, sizeof(b), -120000);
    CHECK(strcmp(b, "-120k") == 0, "-120 kHz was '%s'", b);

    /* Rounds to the nearest kHz. */
    aur_fmt_delta_hz(b, sizeof(b), 37400);
    CHECK(strcmp(b, "+37k") == 0, "+37.4 kHz was '%s'", b);
    aur_fmt_delta_hz(b, sizeof(b), 37600);
    CHECK(strcmp(b, "+38k") == 0, "+37.6 kHz was '%s'", b);

    /* Crosses to MHz at a megahertz, two decimals. */
    aur_fmt_delta_hz(b, sizeof(b), 1230000);
    CHECK(strcmp(b, "+1.23M") == 0, "+1.23 MHz was '%s'", b);
    aur_fmt_delta_hz(b, sizeof(b), -77000000);
    CHECK(strcmp(b, "-77.00M") == 0, "-77 MHz was '%s'", b);

    /* The unit is chosen from the ROUNDED value, so 999.6 kHz is not "1000k". */
    aur_fmt_delta_hz(b, sizeof(b), 999600);
    CHECK(strcmp(b, "+1.00M") == 0, "999.6 kHz was '%s'", b);
    aur_fmt_delta_hz(b, sizeof(b), 999000);
    CHECK(strcmp(b, "+999k") == 0, "999 kHz was '%s'", b);

    /* A whole-band delta still fits the documented 16-byte buffer. */
    aur_fmt_delta_hz(b, sizeof(b), -149000000);
    CHECK(strcmp(b, "-149.00M") == 0, "-149 MHz was '%s'", b);
    CHECK(strlen(b) < 16, "delta string overran the buffer: '%s'", b);
}

int main(void) {
    printf("Aurora - scale engine tests\n");

    test_bands();
    test_zoom_limits();
    test_window_stays_in_band();
    test_zoom_keeps_the_focus();
    test_bin_roundtrip();
    test_bin_saturates();
    test_tick_step();
    test_delta_format();
    test_percentile();
    test_scaling();
    test_dither();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
