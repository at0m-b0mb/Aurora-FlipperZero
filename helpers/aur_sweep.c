#include "aur_sweep.h"

#include <furi_hal.h>
#include <furi_hal_subghz.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

#include <string.h>

#define TAG "Aurora"

#define AUR_WORKER_STACK (2 * 1024)

/* The lower quartile of a sweep is the noise floor: a couple of loud carriers
 * cannot move it, and a genuinely busy band raises it honestly. */
#define AUR_FLOOR_K (AUR_BINS / 4)

/* Floor smoothing. New estimate gets 1/4 weight, so the display settles in
 * under a second without shimmering on every sweep. */
#define AUR_FLOOR_SMOOTH 4

/* Peak-trace decay: 1 dB every this many sweeps, in AurPeakDecay mode. */
#define AUR_PEAK_DECAY_SWEEPS 6

/* Sane starting point before the first sweep has run. */
#define AUR_FLOOR_INIT (-100)

typedef struct {
    uint16_t settle_us;
    uint8_t samples;
} AurDetailSpec;

static const AurDetailSpec aur_detail_specs[AurDetailCount] = {
    [AurDetailFast] = {.settle_us = 320, .samples = 1},
    [AurDetailNormal] = {.settle_us = 700, .samples = 2},
    [AurDetailFine] = {.settle_us = 1500, .samples = 3},
};

struct AurSweep {
    FuriThread* thread;
    FuriMutex* mutex; /* guards the request block and the snapshot */
    volatile bool running;

    /* requested by the GUI, consumed at the top of each sweep */
    AurPlan plan;
    AurDetail detail;
    AurPeakMode peak_mode;
    uint8_t range_db;
    bool plan_dirty;
    bool clear_request;

    AurSweepSnapshot snap;
};

/* ---------------- snapshot maintenance ---------------- */

static void aur_snapshot_clear_traces(AurSweepSnapshot* sn) {
    for(uint8_t i = 0; i < AUR_BINS; i++) {
        sn->peak_dbm[i] = AUR_DBM_INVALID;
        sn->hit[i] = false;
    }
    memset(sn->wf, 0, sizeof(sn->wf));
    sn->wf_head = 0;
    sn->wf_count = 0;
    sn->strongest_dbm = AUR_DBM_INVALID;
    sn->strongest_bin = AUR_BINS / 2;
}

/* ---------------- the worker ---------------- */

static int32_t aur_sweep_thread(void* context) {
    AurSweep* s = context;

    subghz_devices_init();
    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    bool ok = device != NULL;
    if(ok) {
        subghz_devices_begin(device);
        subghz_devices_reset(device);
        /* The 270 kHz OOK preset: the narrowest RX filter the stock presets
         * offer, which is what buys the scanner its frequency selectivity. */
        subghz_devices_load_preset(device, FuriHalSubGhzPresetOok270Async, NULL);
        subghz_devices_set_rx(device);
    }

    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->snap.running = true;
    s->snap.valid = ok;
    s->snap.floor_dbm = AUR_FLOOR_INIT;
    aur_snapshot_clear_traces(&s->snap);
    furi_mutex_release(s->mutex);

    if(!ok) {
        FURI_LOG_E(TAG, "no CC1101");
        while(s->running)
            furi_delay_ms(50);
        subghz_devices_deinit();
        furi_mutex_acquire(s->mutex, FuriWaitForever);
        s->snap.running = false;
        furi_mutex_release(s->mutex);
        return 0;
    }

    AurPlan plan;
    AurDetail detail;
    AurPeakMode peak_mode;
    uint8_t range_db;
    int16_t floor_dbm = AUR_FLOOR_INIT;
    uint32_t decay_tick = 0;
    uint32_t rate_mark = furi_get_tick();
    uint32_t rate_sweeps = 0;

    int16_t dbm[AUR_BINS];
    uint8_t level[AUR_BINS];

    /* Force a full retune on the first pass. */
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    plan = s->plan;
    s->plan_dirty = false;
    detail = s->detail;
    peak_mode = s->peak_mode;
    range_db = s->range_db;
    furi_mutex_release(s->mutex);

    while(s->running) {
        /* --- pick up whatever the GUI asked for since the last sweep --- */
        furi_mutex_acquire(s->mutex, FuriWaitForever);
        bool retuned = s->plan_dirty || s->clear_request;
        plan = s->plan;
        detail = s->detail;
        peak_mode = s->peak_mode;
        range_db = s->range_db;
        s->plan_dirty = false;
        s->clear_request = false;
        if(retuned) {
            aur_snapshot_clear_traces(&s->snap);
            s->snap.plan = plan;
        }
        furi_mutex_release(s->mutex);

        if(retuned) floor_dbm = AUR_FLOOR_INIT;

        const AurDetailSpec* spec = &aur_detail_specs[detail < AurDetailCount ? detail : 0];

        /* --- walk the window --- */
        for(uint8_t bin = 0; bin < AUR_BINS && s->running; bin++) {
            uint32_t freq = aur_bin_freq(&plan, bin);

            if(!subghz_devices_is_frequency_valid(device, freq)) {
                dbm[bin] = AUR_DBM_INVALID;
                continue;
            }

            subghz_devices_idle(device);
            subghz_devices_set_frequency(device, freq);
            subghz_devices_flush_rx(device);
            subghz_devices_set_rx(device);
            furi_delay_us(spec->settle_us);

            /* Take the strongest of a few looks: a burst that only overlaps
             * part of the dwell should still register at its real strength. */
            int16_t best = (int16_t)subghz_devices_get_rssi(device);
            for(uint8_t i = 1; i < spec->samples; i++) {
                furi_delay_us(150);
                int16_t r = (int16_t)subghz_devices_get_rssi(device);
                if(r > best) best = r;
            }
            dbm[bin] = best;
        }
        if(!s->running) break;

        /* --- noise floor, straight out of this sweep --- */
        int16_t sample = aur_percentile(dbm, AUR_BINS, AUR_FLOOR_K);
        if(floor_dbm == AUR_FLOOR_INIT)
            floor_dbm = sample;
        else
            floor_dbm = (int16_t)(
                (floor_dbm * (AUR_FLOOR_SMOOTH - 1) + sample) / AUR_FLOOR_SMOOTH);

        /* --- publish --- */
        decay_tick++;
        bool decay_now = (peak_mode == AurPeakDecay) && (decay_tick % AUR_PEAK_DECAY_SWEEPS == 0);

        furi_mutex_acquire(s->mutex, FuriWaitForever);
        AurSweepSnapshot* sn = &s->snap;
        sn->plan = plan;
        sn->floor_dbm = floor_dbm;

        int16_t strongest = AUR_DBM_INVALID;
        uint8_t strongest_bin = 0;

        for(uint8_t i = 0; i < AUR_BINS; i++) {
            int16_t v = dbm[i];
            sn->dbm[i] = v;
            level[i] = aur_display_level(v, floor_dbm, range_db);
            sn->level[i] = level[i];

            if(v != AUR_DBM_INVALID && v > strongest) {
                strongest = v;
                strongest_bin = i;
            }

            if(peak_mode == AurPeakOff) {
                sn->peak_dbm[i] = AUR_DBM_INVALID;
            } else {
                if(v > sn->peak_dbm[i])
                    sn->peak_dbm[i] = v;
                else if(decay_now && sn->peak_dbm[i] > v)
                    sn->peak_dbm[i]--;
            }

            if(v != AUR_DBM_INVALID && (v - floor_dbm) >= AUR_HIT_DB) {
                if(!sn->hit[i]) {
                    sn->hit[i] = true;
                    sn->hit_events++;
                }
            }
        }

        sn->strongest_dbm = strongest;
        sn->strongest_bin = strongest_bin;

        sn->wf_head = (uint8_t)((sn->wf_head + 1) % AUR_WF_ROWS);
        memcpy(sn->wf[sn->wf_head], level, AUR_BINS);
        if(sn->wf_count < AUR_WF_ROWS) sn->wf_count++;

        sn->sweep_id++;
        rate_sweeps++;

        /* Sweep rate, refreshed about once a second - the waterfall uses it to
         * put its time ticks on real seconds rather than assumed ones. */
        uint32_t now = furi_get_tick();
        uint32_t elapsed = now - rate_mark;
        if(elapsed >= furi_kernel_get_tick_frequency()) {
            uint32_t hz10 = (rate_sweeps * 10u * furi_kernel_get_tick_frequency()) / elapsed;
            sn->sweeps_x10_per_sec = (uint16_t)(hz10 > 65535u ? 65535u : hz10);
            rate_mark = now;
            rate_sweeps = 0;
        }
        furi_mutex_release(s->mutex);
    }

    subghz_devices_idle(device);
    subghz_devices_sleep(device);
    subghz_devices_end(device);
    subghz_devices_deinit();

    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->snap.running = false;
    furi_mutex_release(s->mutex);
    return 0;
}

/* ---------------- API ---------------- */

AurSweep* aur_sweep_alloc(void) {
    AurSweep* s = malloc(sizeof(AurSweep));
    memset(s, 0, sizeof(AurSweep));
    s->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    s->detail = AurDetailNormal;
    s->peak_mode = AurPeakDecay;
    s->range_db = 50;
    aur_plan_init(&s->plan, 1); /* 387-464: where most of the fun lives */
    s->snap.plan = s->plan;
    s->snap.floor_dbm = AUR_FLOOR_INIT;
    aur_snapshot_clear_traces(&s->snap);
    return s;
}

void aur_sweep_free(AurSweep* s) {
    furi_assert(s);
    aur_sweep_stop(s);
    furi_mutex_free(s->mutex);
    free(s);
}

void aur_sweep_start(AurSweep* s) {
    furi_assert(s);
    if(s->running) return;
    s->running = true;
    s->thread = furi_thread_alloc_ex("AuroraSweep", AUR_WORKER_STACK, aur_sweep_thread, s);
    furi_thread_start(s->thread);
}

void aur_sweep_stop(AurSweep* s) {
    furi_assert(s);
    if(!s->running) return;
    s->running = false;
    furi_thread_join(s->thread);
    furi_thread_free(s->thread);
    s->thread = NULL;
}

bool aur_sweep_is_running(AurSweep* s) {
    furi_assert(s);
    return s->running;
}

void aur_sweep_set_plan(AurSweep* s, const AurPlan* plan) {
    furi_assert(s);
    furi_assert(plan);
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    if(s->plan.center != plan->center || s->plan.span != plan->span ||
       s->plan.band != plan->band) {
        s->plan = *plan;
        s->plan_dirty = true;
    } else {
        s->plan = *plan;
    }
    furi_mutex_release(s->mutex);
}

void aur_sweep_set_detail(AurSweep* s, AurDetail detail) {
    furi_assert(s);
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->detail = detail < AurDetailCount ? detail : AurDetailNormal;
    furi_mutex_release(s->mutex);
}

void aur_sweep_set_peak_mode(AurSweep* s, AurPeakMode mode) {
    furi_assert(s);
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->peak_mode = mode < AurPeakCount ? mode : AurPeakDecay;
    furi_mutex_release(s->mutex);
}

void aur_sweep_set_range_db(AurSweep* s, uint8_t range_db) {
    furi_assert(s);
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->range_db = range_db ? range_db : 50;
    furi_mutex_release(s->mutex);
}

void aur_sweep_clear(AurSweep* s) {
    furi_assert(s);
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->clear_request = true;
    furi_mutex_release(s->mutex);
}

void aur_sweep_get(AurSweep* s, AurSweepSnapshot* out) {
    furi_assert(s);
    furi_assert(out);
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    *out = s->snap;
    furi_mutex_release(s->mutex);
}
