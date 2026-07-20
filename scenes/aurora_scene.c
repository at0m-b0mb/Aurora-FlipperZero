#include "../aurora_i.h"

// Generate on_enter handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const aurora_scene_on_enter_handlers[])(void*) = {
#include "aurora_scene_config.h"
};
#undef ADD_SCENE

// Generate on_event handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const aurora_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "aurora_scene_config.h"
};
#undef ADD_SCENE

// Generate on_exit handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const aurora_scene_on_exit_handlers[])(void* context) = {
#include "aurora_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers aurora_scene_handlers = {
    .on_enter_handlers = aurora_scene_on_enter_handlers,
    .on_event_handlers = aurora_scene_on_event_handlers,
    .on_exit_handlers = aurora_scene_on_exit_handlers,
    .scene_num = AuroraSceneNum,
};
