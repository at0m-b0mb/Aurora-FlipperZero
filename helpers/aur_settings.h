/**
 * Aurora - settings persistence.
 *
 * The band you scan, how finely, how the display is tuned and whether it
 * chirps are all things you set once and want to find again after a reboot.
 * This reads and writes them to a small text file on the SD card in the
 * Flipper's own FlipperFormat, so the config is human-readable and survives a
 * firmware update the way a binary blob would not.
 *
 * Every field is validated on load: a hand-edited or version-skewed file can
 * only ever move a setting to a valid value, never crash the app or index an
 * array out of bounds. A missing file is not an error - it just means the
 * built-in defaults stand.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The persisted slice of app state. Plain integers, so it maps straight onto
 * the on-disk uint32 fields with no packing surprises. */
typedef struct {
    uint8_t band_index;
    uint8_t detail;
    uint8_t range_index;
    uint8_t peak_mode;
    uint8_t view_mode; /* the face you left the scanner on */
    bool sound;
    bool led;
} AurSettings;

/** Fill `s` with the built-in defaults. Always valid. */
void aur_settings_default(AurSettings* s);

/**
 * Load from the SD card, clamping every field into range. Returns true if a
 * file was read; false (with defaults applied) if there was none. Either way
 * `s` is left valid and safe to use.
 */
bool aur_settings_load(AurSettings* s);

/** Persist to the SD card. Returns false if the write failed. */
bool aur_settings_save(const AurSettings* s);

#ifdef __cplusplus
}
#endif
