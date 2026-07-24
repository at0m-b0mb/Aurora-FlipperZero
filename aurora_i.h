#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "aurora_icons.h" // generated from icons/ by fbt

#include "helpers/aur_scale.h"
#include "helpers/aur_sweep.h"
#include "helpers/aur_settings.h"
#include "views/scanner_view.h"
#include "scenes/aurora_scene.h"

#define AURORA_VERSION "1.1"

/** How long the control legend stays up when the scanner opens, in ms. */
#define AURORA_HINT_MS 2600

/** Floor on the gap between two activity chirps, in ms. */
#define AURORA_ALERT_GAP_MS 400

typedef enum {
    AuroraViewSubmenu,
    AuroraViewScanner,
    AuroraViewSettings,
    AuroraViewText,
} AuroraViewId;

typedef enum {
    /* Scanner view events arrive offset from here, so the scene can decode
     * them with a single subtraction. */
    AuroraCustomEventScannerBase = 100,
} AuroraCustomEvent;

/** Display dynamic range, in dB mapped across the full scale. */
#define AURORA_RANGE_COUNT 3
extern const uint8_t aurora_range_db[AURORA_RANGE_COUNT];

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;

    ScannerView* scanner_view;
    AurSweep* sweep;

    /* Scratch for aur_sweep_get. Roughly 3 KB - far too big for the GUI
     * thread's stack, so it lives on the heap for the app's whole life. */
    AurSweepSnapshot* snap;

    AurSettings settings; /* persisted across reboots */

    /* live scanner state */
    AurPlan plan;
    uint32_t cursor_freq;
    uint8_t mode; /* AurViewMode */
    bool hold;

    /* measurement marker: a frequency you drop to read Δf / ΔdB against the
     * cursor. Stored as a frequency so it survives zoom and pan. */
    bool marker_set;
    uint32_t marker_freq;

    uint32_t hint_until;
    uint32_t last_hit_events;
    uint32_t last_alert;
} AuroraApp;

/** Push every setting into the sweeper. */
void aurora_apply_settings(AuroraApp* app);

/** Persist current settings to the SD card (best effort). */
void aurora_save_settings(AuroraApp* app);

/* feedback, gated by settings */
void aurora_notify_hit(AuroraApp* app);
void aurora_notify_click(AuroraApp* app);
void aurora_notify_edge(AuroraApp* app);
