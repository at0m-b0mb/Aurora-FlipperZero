#include "../aurora_i.h"

void aurora_scene_about_on_enter(void* context) {
    AuroraApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    widget_add_text_scroll_element(
        widget,
        0,
        0,
        128,
        64,
        "\e#Aurora " AURORA_VERSION "\e#\n"
        "See what is on the air\n"
        "around you.\n"
        "\n"
        "\e#What it does\e#\n"
        "Aurora walks the internal\n"
        "CC1101 across a Sub-GHz band\n"
        "in 64 steps, reading signal\n"
        "strength at each one. Every\n"
        "pass draws one spectrum trace\n"
        "and adds one row to a\n"
        "scrolling waterfall, so a\n"
        "burst that lasted a tenth of\n"
        "a second is still on screen\n"
        "seconds later.\n"
        "\n"
        "\e#Markers\e#\n"
        "Hold Up to drop a marker on a\n"
        "signal, then move the cursor:\n"
        "the strip reads the frequency\n"
        "and dB gap between the two.\n"
        "The quick way to measure a\n"
        "channel spacing.\n"
        "\n"
        "\e#The noise floor\e#\n"
        "The floor is read out of each\n"
        "sweep as the quarter-point of\n"
        "its own 64 readings, so the\n"
        "display calibrates itself to\n"
        "wherever you are standing. A\n"
        "few loud carriers cannot drag\n"
        "a quartile the way they would\n"
        "drag an average.\n"
        "\n"
        "\e#Honest limits\e#\n"
        "- The CC1101's receive filter\n"
        "  is 270-650 kHz wide. A\n"
        "  carrier smears across\n"
        "  several bins no matter how\n"
        "  far you zoom, so zoom buys\n"
        "  you pointing accuracy, not\n"
        "  true resolution. Aurora\n"
        "  stops at 2 MHz rather than\n"
        "  imply otherwise.\n"
        "- It samples one bin at a\n"
        "  time. A burst shorter than\n"
        "  a sweep can be missed, or\n"
        "  land in one bin only.\n"
        "- dBm is the chip's own RSSI,\n"
        "  uncalibrated. Compare\n"
        "  readings to each other, not\n"
        "  to a spec sheet.\n"
        "- Only the three ranges the\n"
        "  firmware permits: 300-348,\n"
        "  387-464 and 779-928 MHz.\n"
        "- It does not decode. For\n"
        "  what a signal says, use\n"
        "  the stock Sub-GHz app.\n"
        "\n"
        "Listen-only. Aurora tunes and\n"
        "measures. It never transmits.\n"
        "\n"
        "\e#Credits\e#\n"
        "by at0m-b0mb\n"
        "MIT licensed\n"
        "github.com/at0m-b0mb/\n"
        "Aurora-FlipperZero\n");

    view_dispatcher_switch_to_view(app->view_dispatcher, AuroraViewText);
}

bool aurora_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void aurora_scene_about_on_exit(void* context) {
    AuroraApp* app = context;
    widget_reset(app->widget);
}
