#include "aurora_i.h"
#include <string.h>

const uint8_t aurora_range_db[AURORA_RANGE_COUNT] = {30, 50, 70};

/* ---------------- feedback ----------------
 * A scanner is something you watch, so the audio stays out of the way: a
 * short chirp when a quiet channel first comes alive, a tick for a control,
 * and a lower tick when a control refuses to go further.
 */
static const NotificationSequence seq_snd_hit = {
    &message_note_a5,
    &message_delay_25,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_snd_click = {
    &message_note_c6,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_snd_edge = {
    &message_note_c4,
    &message_delay_25,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_led_hit = {
    &message_green_255,
    &message_delay_25,
    &message_green_0,
    NULL,
};

void aurora_notify_hit(AuroraApp* app) {
    furi_assert(app);
    if(app->settings.led) notification_message(app->notifications, &seq_led_hit);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_hit);
}

void aurora_notify_click(AuroraApp* app) {
    furi_assert(app);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_click);
}

void aurora_notify_edge(AuroraApp* app) {
    furi_assert(app);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_edge);
}

/* ---------------- settings ---------------- */

void aurora_apply_settings(AuroraApp* app) {
    furi_assert(app);
    aur_sweep_set_detail(app->sweep, (AurDetail)app->settings.detail);
    aur_sweep_set_peak_mode(app->sweep, (AurPeakMode)app->settings.peak_mode);
    aur_sweep_set_range_db(app->sweep, aurora_range_db[app->settings.range_index]);
}

void aurora_save_settings(AuroraApp* app) {
    furi_assert(app);
    /* The view you leave on is part of the state worth keeping. */
    app->settings.view_mode = app->mode;
    aur_settings_save(&app->settings);
}

/* ---------------- view dispatcher plumbing ---------------- */

static bool aurora_custom_event_callback(void* context, uint32_t event) {
    AuroraApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool aurora_back_event_callback(void* context) {
    AuroraApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void aurora_tick_event_callback(void* context) {
    AuroraApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ---------------- lifecycle ---------------- */

static AuroraApp* aurora_app_alloc(void) {
    AuroraApp* app = malloc(sizeof(AuroraApp));
    memset(app, 0, sizeof(AuroraApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&aurora_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, aurora_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, aurora_back_event_callback);
    /* 50 ms: fast enough that the display keeps up with a Fast sweep, and the
     * waterfall itself is buffered by the sweeper so no row is ever dropped. */
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, aurora_tick_event_callback, 50);

    // settings: whatever was last saved, or the built-in defaults on first run
    aur_settings_load(&app->settings);
    app->mode = app->settings.view_mode;

    app->sweep = aur_sweep_alloc();
    app->snap = malloc(sizeof(AurSweepSnapshot));
    memset(app->snap, 0, sizeof(AurSweepSnapshot));

    aur_plan_init(&app->plan, app->settings.band_index);
    app->cursor_freq = app->plan.center;
    /* Give the scratch snapshot a real plan up front. The scanner derives the
     * cursor's column from the snapshot's plan, and a zeroed one has a span of
     * zero - which would pin the cursor to the last column on the first frame. */
    app->snap->plan = app->plan;

    // shared GUI modules
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AuroraViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AuroraViewSettings, variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AuroraViewText, widget_get_view(app->widget));

    app->scanner_view = scanner_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AuroraViewScanner, scanner_view_get_view(app->scanner_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void aurora_app_free(AuroraApp* app) {
    furi_assert(app);

    /* Last chance to persist - covers the view mode, which only ever changes
     * inside the scanner and so is never saved by the settings screen. */
    aurora_save_settings(app);

    aur_sweep_stop(app->sweep);

    view_dispatcher_remove_view(app->view_dispatcher, AuroraViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, AuroraViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, AuroraViewText);
    view_dispatcher_remove_view(app->view_dispatcher, AuroraViewScanner);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    scanner_view_free(app->scanner_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    aur_sweep_free(app->sweep);
    free(app->snap);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t aurora_app(void* p) {
    UNUSED(p);
    AuroraApp* app = aurora_app_alloc();
    scene_manager_next_scene(app->scene_manager, AuroraSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    aurora_app_free(app);
    return 0;
}
