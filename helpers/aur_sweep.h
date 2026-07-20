/**
 * Aurora - the sweeping receiver.
 *
 * Parks the internal CC1101 in RX and walks it across the planned window one
 * bin at a time, reading RSSI at each stop. Sixty-four stops make a sweep;
 * every sweep becomes one live spectrum trace and one new row of waterfall.
 *
 * The noise floor is read straight out of each sweep as a low percentile of
 * its own 64 readings, so the display self-calibrates to wherever you are
 * standing instead of trusting a constant. Strictly listen-only: Aurora tunes
 * and measures, and never keys the transmitter.
 */
#pragma once

#include <furi.h>
#include "aur_scale.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Dwell per bin. Slower means a quieter, steadier trace. */
typedef enum {
    AurDetailFast, /* ~20 sweeps/s, noisier */
    AurDetailNormal,
    AurDetailFine, /* ~6 sweeps/s, clean    */
    AurDetailCount,
} AurDetail;

/** What survives on the peak trace between sweeps. */
typedef enum {
    AurPeakOff,
    AurPeakDecay, /* fades back to live over a few seconds */
    AurPeakHold, /* never forgets until the plan changes  */
    AurPeakCount,
} AurPeakMode;

/** dB above the floor that counts as "something is transmitting here". */
#define AUR_HIT_DB 12

/** Everything the display needs, copied out under one lock. */
typedef struct {
    bool running;
    bool valid; /* the radio came up                       */

    AurPlan plan; /* the plan THIS data was measured against  */
    uint32_t sweep_id; /* completed sweeps since start            */

    int16_t dbm[AUR_BINS]; /* live reading per bin                    */
    uint8_t level[AUR_BINS]; /* live reading as 0..16                   */
    int16_t peak_dbm[AUR_BINS]; /* peak trace per bin                      */
    bool hit[AUR_BINS]; /* has ever crossed AUR_HIT_DB here        */

    int16_t floor_dbm; /* smoothed noise floor                    */
    int16_t strongest_dbm;
    uint8_t strongest_bin;

    uint32_t hit_events; /* bumps when a quiet bin first goes loud  */
    uint16_t sweeps_x10_per_sec; /* measured sweep rate, x10          */

    uint8_t wf[AUR_WF_ROWS][AUR_BINS]; /* newest row at wf_head        */
    uint8_t wf_head;
    uint8_t wf_count;
} AurSweepSnapshot;

typedef struct AurSweep AurSweep;

AurSweep* aur_sweep_alloc(void);
void aur_sweep_free(AurSweep* s);

void aur_sweep_start(AurSweep* s);
void aur_sweep_stop(AurSweep* s);
bool aur_sweep_is_running(AurSweep* s);

/** Retune. A changed plan clears the peak trace, the hits and the waterfall. */
void aur_sweep_set_plan(AurSweep* s, const AurPlan* plan);

void aur_sweep_set_detail(AurSweep* s, AurDetail detail);
void aur_sweep_set_peak_mode(AurSweep* s, AurPeakMode mode);
void aur_sweep_set_range_db(AurSweep* s, uint8_t range_db);

/** Drop the peak trace, the hit map and the waterfall without retuning. */
void aur_sweep_clear(AurSweep* s);

/** Copy the current state out. `out` is large - keep it off the stack. */
void aur_sweep_get(AurSweep* s, AurSweepSnapshot* out);

#ifdef __cplusplus
}
#endif
