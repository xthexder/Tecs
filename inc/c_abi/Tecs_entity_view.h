#pragma once

#include "Tecs_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct TecsEntityView {
    const void *storage = nullptr;
    size_t start_index = 0;
    size_t end_index = 0;
} TecsEntityView;

size_t Tecs_entity_view_storage_size(const TecsEntityView *view);
const TecsEntity *Tecs_entity_view_begin(const TecsEntityView *view);
const TecsEntity *Tecs_entity_view_end(const TecsEntityView *view);

#ifdef __cplusplus
}
#endif