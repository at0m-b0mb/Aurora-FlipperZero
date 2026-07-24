#include "aur_settings.h"
#include "aur_scale.h" /* AUR_BAND_COUNT - the one bound the file must respect */

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG "Aurora"

/* /data is this app's private storage - it maps to
 * /ext/apps_data/aurora/ on the SD card and needs no mkdir. */
#define AUR_CONF_PATH APP_DATA_PATH("aurora.conf")
#define AUR_CONF_HEADER "Aurora settings"
#define AUR_CONF_VERSION 1

/* These mirror the enum counts in the app. Keeping them as local constants
 * rather than pulling the headers in means the clamp cannot silently widen if
 * an enum grows - it fails safe (rejects the value) until updated on purpose. */
#define AUR_DETAIL_MAX 2 /* Fast, Normal, Fine        */
#define AUR_RANGE_MAX 2 /* 30, 50, 70 dB             */
#define AUR_PEAK_MAX 2 /* Off, Decay, Hold          */
#define AUR_VIEW_MAX 2 /* Split, Waterfall, Spectrum */

void aur_settings_default(AurSettings* s) {
    furi_assert(s);
    s->band_index = 1; /* 387-464 MHz - the busy one */
    s->detail = 1; /* Normal                     */
    s->range_index = 1; /* 50 dB                      */
    s->peak_mode = 1; /* Decay                      */
    s->view_mode = 0; /* Split                      */
    s->sound = true;
    s->led = true;
}

static uint8_t clamp_u8(uint32_t v, uint8_t hi) {
    return v > hi ? hi : (uint8_t)v;
}

/* Read one uint32 key, leaving the destination untouched if the key is
 * absent - so a file written by an older version keeps this version's default
 * for any field it never knew about. */
static void read_u8(FlipperFormat* ff, const char* key, uint8_t* dst, uint8_t hi) {
    uint32_t v = 0;
    flipper_format_rewind(ff);
    if(flipper_format_read_uint32(ff, key, &v, 1)) *dst = clamp_u8(v, hi);
}

static void read_bool(FlipperFormat* ff, const char* key, bool* dst) {
    uint32_t v = 0;
    flipper_format_rewind(ff);
    if(flipper_format_read_uint32(ff, key, &v, 1)) *dst = v != 0;
}

bool aur_settings_load(AurSettings* s) {
    furi_assert(s);
    aur_settings_default(s);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool found = false;

    do {
        if(!flipper_format_file_open_existing(ff, AUR_CONF_PATH)) break;

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        bool header_ok = flipper_format_read_header(ff, header, &version) &&
                         furi_string_equal(header, AUR_CONF_HEADER);
        furi_string_free(header);
        if(!header_ok) break; /* not our file - leave defaults standing */

        read_u8(ff, "Band", &s->band_index, (uint8_t)(AUR_BAND_COUNT - 1));
        read_u8(ff, "Detail", &s->detail, AUR_DETAIL_MAX);
        read_u8(ff, "Range", &s->range_index, AUR_RANGE_MAX);
        read_u8(ff, "PeakMode", &s->peak_mode, AUR_PEAK_MAX);
        read_u8(ff, "View", &s->view_mode, AUR_VIEW_MAX);
        read_bool(ff, "Sound", &s->sound);
        read_bool(ff, "Led", &s->led);
        found = true;
    } while(false);

    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return found;
}

bool aur_settings_save(const AurSettings* s) {
    furi_assert(s);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;

    do {
        if(!flipper_format_file_open_always(ff, AUR_CONF_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, AUR_CONF_HEADER, AUR_CONF_VERSION)) break;
        flipper_format_write_comment_cstr(ff, "Aurora scanner - edited by hand at your own risk");

        uint32_t v;
        v = s->band_index;
        if(!flipper_format_write_uint32(ff, "Band", &v, 1)) break;
        v = s->detail;
        if(!flipper_format_write_uint32(ff, "Detail", &v, 1)) break;
        v = s->range_index;
        if(!flipper_format_write_uint32(ff, "Range", &v, 1)) break;
        v = s->peak_mode;
        if(!flipper_format_write_uint32(ff, "PeakMode", &v, 1)) break;
        v = s->view_mode;
        if(!flipper_format_write_uint32(ff, "View", &v, 1)) break;
        v = s->sound ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "Sound", &v, 1)) break;
        v = s->led ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "Led", &v, 1)) break;
        ok = true;
    } while(false);

    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
