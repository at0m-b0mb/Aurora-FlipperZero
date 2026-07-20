/**
 * Aurora - the scanner display.
 *
 * Three faces over the same sweep: a live spectrum above a scrolling
 * waterfall, the waterfall alone, or the spectrum alone. The waterfall is
 * ordered-dithered, which is what makes a one-bit screen show signal strength
 * as apparent brightness instead of a binary on/off smear.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/aur_sweep.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AurModeSplit, /* spectrum + waterfall */
    AurModeWaterfall, /* waterfall, full height */
    AurModeSpectrum, /* spectrum, full height + activity map */
    AurModeCount,
} AurViewMode;

typedef enum {
    ScannerEventCursorLeft,
    ScannerEventCursorRight,
    ScannerEventCursorLeftFast, /* key held */
    ScannerEventCursorRightFast,
    ScannerEventZoomIn,
    ScannerEventZoomOut,
    ScannerEventCycleMode,
    ScannerEventSnapPeak, /* park the cursor on the strongest bin */
    ScannerEventToggleHold, /* freeze the display */
} ScannerViewEvent;

typedef void (*ScannerViewEventCallback)(void* context, ScannerViewEvent event);

/** Per-frame state the scene owns; the snapshot carries everything else. */
typedef struct {
    uint8_t mode; /* AurViewMode                        */
    bool hold; /* display frozen                     */
    uint8_t range_db; /* dB mapped onto the full scale      */
    uint32_t cursor_freq;
    uint8_t cursor_bin;
    bool show_hint; /* transient control legend           */
} ScannerUi;

typedef struct ScannerView ScannerView;

ScannerView* scanner_view_alloc(void);
void scanner_view_free(ScannerView* v);
View* scanner_view_get_view(ScannerView* v);

void scanner_view_set_event_callback(ScannerView* v, ScannerViewEventCallback cb, void* context);

/** Publish a frame. Both arguments are copied. */
void scanner_view_update(ScannerView* v, const AurSweepSnapshot* sn, const ScannerUi* ui);

#ifdef __cplusplus
}
#endif
