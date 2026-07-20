#include "../aurora_i.h"

void aurora_scene_controls_on_enter(void* context) {
    AuroraApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    widget_add_text_scroll_element(
        widget,
        0,
        0,
        128,
        64,
        "\e#Controls\e#\n"
        "\n"
        "\e#Left / Right\e#\n"
        "Move the cursor one bin.\n"
        "Hold to scrub. Push past an\n"
        "edge and the window pans.\n"
        "\n"
        "\e#Up / Down\e#\n"
        "Zoom in / out, centred on the\n"
        "cursor. Zoom 0 is the whole\n"
        "band; each step halves it.\n"
        "\n"
        "\e#OK\e#\n"
        "Cycle the view:\n"
        "  Split - spectrum + waterfall\n"
        "  Waterfall - full height,\n"
        "    with 1-second ticks\n"
        "  Spectrum - full height,\n"
        "    plus the activity map\n"
        "\n"
        "\e#OK (hold)\e#\n"
        "Snap the cursor onto the\n"
        "strongest bin in the sweep.\n"
        "\n"
        "\e#Back (hold)\e#\n"
        "Freeze the display so you can\n"
        "read a burst that has already\n"
        "gone. Hold again to resume.\n"
        "\n"
        "\e#Back\e#\n"
        "Leave the scanner.\n"
        "\n"
        "\e#Reading it\e#\n"
        "Header: the cursor's frequency\n"
        "and its level in dBm.\n"
        "Strip: the span on the left,\n"
        "and the loudest bin right now\n"
        "on the right.\n"
        "Dotted row under the spectrum:\n"
        "round-frequency gridlines.\n"
        "Thin caps above the bars: the\n"
        "peak trace.\n"
        "Top row in Spectrum view: every\n"
        "bin that has been busy since\n"
        "you tuned here.\n");

    view_dispatcher_switch_to_view(app->view_dispatcher, AuroraViewText);
}

bool aurora_scene_controls_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void aurora_scene_controls_on_exit(void* context) {
    AuroraApp* app = context;
    widget_reset(app->widget);
}
