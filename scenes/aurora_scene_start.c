#include "../aurora_i.h"

typedef enum {
    StartIndexScan,
    StartIndexSettings,
    StartIndexControls,
    StartIndexAbout,
} StartIndex;

static void aurora_scene_start_submenu_cb(void* context, uint32_t index) {
    AuroraApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void aurora_scene_start_on_enter(void* context) {
    AuroraApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Aurora");
    submenu_add_item(submenu, "Scan the band", StartIndexScan, aurora_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Settings", StartIndexSettings, aurora_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Controls", StartIndexControls, aurora_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "About", StartIndexAbout, aurora_scene_start_submenu_cb, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, AuroraSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, AuroraViewSubmenu);
}

bool aurora_scene_start_on_event(void* context, SceneManagerEvent event) {
    AuroraApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, AuroraSceneStart, event.event);
        switch(event.event) {
        case StartIndexScan:
            scene_manager_next_scene(app->scene_manager, AuroraSceneScan);
            consumed = true;
            break;
        case StartIndexSettings:
            scene_manager_next_scene(app->scene_manager, AuroraSceneSettings);
            consumed = true;
            break;
        case StartIndexControls:
            scene_manager_next_scene(app->scene_manager, AuroraSceneControls);
            consumed = true;
            break;
        case StartIndexAbout:
            scene_manager_next_scene(app->scene_manager, AuroraSceneAbout);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void aurora_scene_start_on_exit(void* context) {
    AuroraApp* app = context;
    submenu_reset(app->submenu);
}
