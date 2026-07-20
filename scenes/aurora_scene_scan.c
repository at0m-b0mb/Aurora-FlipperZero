#include "../aurora_i.h"
#include <string.h>

/* Bins the cursor jumps per repeat when a direction key is held down. */
#define SCAN_FAST_BINS 4

/* Fraction of a span the window pans by when the cursor walks off an edge. */
#define SCAN_PAN_DIV 4

static uint32_t ms_to_ticks(uint32_t ms) {
    return (ms * furi_kernel_get_tick_frequency()) / 1000u;
}

static void aurora_scan_event_cb(void* context, ScannerViewEvent event) {
    AuroraApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, AuroraCustomEventScannerBase + (uint32_t)event);
}

/* ---------------- cursor, pan, zoom ---------------- */

static void scan_retune(AuroraApp* app) {
    aur_sweep_set_plan(app->sweep, &app->plan);
    app->cursor_freq = aur_clamp_cursor(&app->plan, app->cursor_freq);
}

/**
 * Slide the window sideways. Only meaningful once you have zoomed in - at zoom
 * 0 the whole band is already on screen, so walking off the edge is genuinely
 * the end of the road and says so.
 */
static void scan_pan(AuroraApp* app, int8_t dir) {
    /* Panning a frozen display would retune the radio behind a picture that
     * cannot update - the window and the pixels would disagree. Moving the
     * cursor across a frozen sweep is the whole point of freezing, so that
     * still works; walking off the edge simply stops here. */
    if(app->hold || app->plan.zoom == 0) {
        aurora_notify_edge(app);
        return;
    }

    uint32_t step = app->plan.span / SCAN_PAN_DIV;
    uint32_t before = app->plan.center;
    uint32_t want = (dir < 0) ? (before > step ? before - step : 0) : before + step;

    AurPlan p = app->plan;
    aur_plan_zoom(&p, 0, want); /* same zoom, new centre, re-clamped to band */
    if(p.center == before) {
        aurora_notify_edge(app);
        return;
    }

    app->plan = p;
    scan_retune(app);
}

static void scan_cursor_move(AuroraApp* app, int8_t bins) {
    uint8_t bin = aur_freq_bin(&app->plan, app->cursor_freq);
    int16_t next = (int16_t)bin + bins;

    if(next < 0) {
        app->cursor_freq = aur_bin_freq(&app->plan, 0);
        scan_pan(app, -1);
        return;
    }
    if(next > (int16_t)(AUR_BINS - 1)) {
        app->cursor_freq = aur_bin_freq(&app->plan, AUR_BINS - 1);
        scan_pan(app, +1);
        return;
    }
    app->cursor_freq = aur_bin_freq(&app->plan, (uint8_t)next);
}

static void scan_zoom(AuroraApp* app, int8_t delta) {
    /* You cannot zoom into data that has stopped arriving, so asking to zoom
     * is taken as asking to resume. */
    app->hold = false;

    uint8_t before = app->plan.zoom;
    /* Zoom around the cursor, not the centre: that is what lets you walk a
     * peak from "the whole ISM band" down to "that channel" in four presses
     * without ever losing sight of it. */
    aur_plan_zoom(&app->plan, delta, app->cursor_freq);

    if(app->plan.zoom == before)
        aurora_notify_edge(app);
    else
        aurora_notify_click(app);

    scan_retune(app);
}

static void scan_snap_peak(AuroraApp* app) {
    if(app->snap->strongest_dbm == AUR_DBM_INVALID) {
        aurora_notify_edge(app);
        return;
    }
    app->cursor_freq =
        aur_clamp_cursor(&app->plan, aur_bin_freq(&app->snap->plan, app->snap->strongest_bin));
    aurora_notify_click(app);
}

/* ---------------- frame ---------------- */

static void scan_publish(AuroraApp* app) {
    ScannerUi ui;
    memset(&ui, 0, sizeof(ui));
    ui.mode = app->mode;
    ui.hold = app->hold;
    ui.range_db = aurora_range_db[app->settings.range_index];
    ui.cursor_freq = app->cursor_freq;
    /* Against the snapshot's plan, not the live one: a retune takes a sweep to
     * land, and for that one frame the cursor must line up with the data that
     * is actually on screen. */
    ui.cursor_bin = aur_freq_bin(&app->snap->plan, app->cursor_freq);
    ui.show_hint = furi_get_tick() < app->hint_until;

    scanner_view_update(app->scanner_view, app->snap, &ui);
}

/* ---------------- scene ---------------- */

void aurora_scene_scan_on_enter(void* context) {
    AuroraApp* app = context;

    /* A band change in Settings resets the plan; anything else keeps the
     * window you left, so stepping out to Settings and back does not throw
     * away the peak you were chasing. */
    if(app->plan.band != app->settings.band_index) {
        aur_plan_init(&app->plan, app->settings.band_index);
        app->cursor_freq = app->plan.center;
    }

    aurora_apply_settings(app);
    aur_sweep_set_plan(app->sweep, &app->plan);
    aur_sweep_start(app->sweep);

    scanner_view_set_event_callback(app->scanner_view, aurora_scan_event_cb, app);

    app->hold = false;
    app->hint_until = furi_get_tick() + ms_to_ticks(AURORA_HINT_MS);
    app->last_hit_events = 0;
    app->last_alert = 0;

    view_dispatcher_switch_to_view(app->view_dispatcher, AuroraViewScanner);
}

bool aurora_scene_scan_on_event(void* context, SceneManagerEvent event) {
    AuroraApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event < AuroraCustomEventScannerBase) return false;
        ScannerViewEvent e = (ScannerViewEvent)(event.event - AuroraCustomEventScannerBase);

        app->hint_until = 0; // the first press has taught the lesson

        switch(e) {
        case ScannerEventCursorLeft:
            scan_cursor_move(app, -1);
            break;
        case ScannerEventCursorRight:
            scan_cursor_move(app, +1);
            break;
        case ScannerEventCursorLeftFast:
            scan_cursor_move(app, -SCAN_FAST_BINS);
            break;
        case ScannerEventCursorRightFast:
            scan_cursor_move(app, +SCAN_FAST_BINS);
            break;
        case ScannerEventZoomIn:
            scan_zoom(app, +1);
            break;
        case ScannerEventZoomOut:
            scan_zoom(app, -1);
            break;
        case ScannerEventCycleMode:
            app->mode = (uint8_t)((app->mode + 1) % AurModeCount);
            aurora_notify_click(app);
            break;
        case ScannerEventSnapPeak:
            scan_snap_peak(app);
            break;
        case ScannerEventToggleHold:
            app->hold = !app->hold;
            aurora_notify_click(app);
            break;
        default:
            break;
        }

        /* Redraw immediately: a control that waits up to a tick to show its
         * effect feels broken, and a frozen display would not redraw at all. */
        scan_publish(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeTick) {
        if(!app->hold) {
            aur_sweep_get(app->sweep, app->snap);

            /* Chirp when a channel that was quiet comes alive. The sweep-id
             * guard keeps the first sweep - which discovers every already-busy
             * channel at once - from sounding like an alarm. */
            if(app->snap->hit_events > app->last_hit_events) {
                uint32_t now = furi_get_tick();
                if(app->snap->sweep_id > 2 && (now - app->last_alert) > ms_to_ticks(AURORA_ALERT_GAP_MS)) {
                    aurora_notify_hit(app);
                    app->last_alert = now;
                }
                app->last_hit_events = app->snap->hit_events;
            }

            scan_publish(app);
        }
        return true;
    }

    return false;
}

void aurora_scene_scan_on_exit(void* context) {
    AuroraApp* app = context;
    scanner_view_set_event_callback(app->scanner_view, NULL, NULL);
    aur_sweep_stop(app->sweep);
}
