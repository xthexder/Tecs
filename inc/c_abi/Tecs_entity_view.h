#pragma once

#include "Tecs_entity.h"
#include "Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct TecsEntityView {
    const void *storage;
    size_t start_index;
    size_t end_index;
} TecsEntityView;

TECS_EXPORT size_t Tecs_entity_view_storage_size(const TecsEntityView *view);
TECS_EXPORT const TecsEntity *Tecs_entity_view_begin(const TecsEntityView *view);
TECS_EXPORT const TecsEntity *Tecs_entity_view_end(const TecsEntityView *view);

#ifdef __cplusplus
}
#endif
