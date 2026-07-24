#include "../aurora_i.h"

static const char* const on_off[] = {"OFF", "ON"};
static const char* const detail_names[AurDetailCount] = {"Fast", "Normal", "Fine"};
static const char* const peak_names[AurPeakCount] = {"Off", "Decay", "Hold"};
static const char* const range_names[AURORA_RANGE_COUNT] = {"30 dB", "50 dB", "70 dB"};

static void aurora_settings_band_cb(VariableItem* item) {
    AuroraApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.band_index = idx;
    variable_item_set_current_value_text(item, aur_bands[idx].label);
}

static void aurora_settings_detail_cb(VariableItem* item) {
    AuroraApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.detail = idx;
    variable_item_set_current_value_text(item, detail_names[idx]);
}

static void aurora_settings_range_cb(VariableItem* item) {
    AuroraApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.range_index = idx;
    variable_item_set_current_value_text(item, range_names[idx]);
}

static void aurora_settings_peak_cb(VariableItem* item) {
    AuroraApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.peak_mode = idx;
    variable_item_set_current_value_text(item, peak_names[idx]);
}

static void aurora_settings_sound_cb(VariableItem* item) {
    AuroraApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.sound = idx > 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

static void aurora_settings_led_cb(VariableItem* item) {
    AuroraApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.led = idx > 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

void aurora_scene_settings_on_enter(void* context) {
    AuroraApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(list, "Band", AUR_BAND_COUNT, aurora_settings_band_cb, app);
    variable_item_set_current_value_index(item, app->settings.band_index);
    variable_item_set_current_value_text(item, aur_bands[app->settings.band_index].label);

    item = variable_item_list_add(list, "Detail", AurDetailCount, aurora_settings_detail_cb, app);
    variable_item_set_current_value_index(item, app->settings.detail);
    variable_item_set_current_value_text(item, detail_names[app->settings.detail]);

    item = variable_item_list_add(list, "Range", AURORA_RANGE_COUNT, aurora_settings_range_cb, app);
    variable_item_set_current_value_index(item, app->settings.range_index);
    variable_item_set_current_value_text(item, range_names[app->settings.range_index]);

    item = variable_item_list_add(list, "Peak trace", AurPeakCount, aurora_settings_peak_cb, app);
    variable_item_set_current_value_index(item, app->settings.peak_mode);
    variable_item_set_current_value_text(item, peak_names[app->settings.peak_mode]);

    item = variable_item_list_add(list, "Chirp on hit", 2, aurora_settings_sound_cb, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.sound ? 1 : 0]);

    item = variable_item_list_add(list, "LED", 2, aurora_settings_led_cb, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.led ? 1 : 0]);

    view_dispatcher_switch_to_view(app->view_dispatcher, AuroraViewSettings);
}

bool aurora_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void aurora_scene_settings_on_exit(void* context) {
    AuroraApp* app = context;
    variable_item_list_reset(app->var_item_list);
    /* Detail, range and peak mode take effect on the next sweep. Band is
     * handled by the scan scene, which rebuilds the plan around it. */
    aurora_apply_settings(app);
    aurora_save_settings(app);
}
