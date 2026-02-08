#pragma once

#include "Tecs_entity.h"
#include "Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct tecs_entity_view_t {
    const void *storage;
    uint64_t start_index;
    uint64_t end_index;
} tecs_entity_view_t;

TECS_EXPORT uint64_t Tecs_entity_view_storage_size(const tecs_entity_view_t *view);
TECS_EXPORT const tecs_entity_t *Tecs_entity_view_begin(const tecs_entity_view_t *view);
TECS_EXPORT const tecs_entity_t *Tecs_entity_view_end(const tecs_entity_view_t *view);

#ifdef __cplusplus
} // extern "C"
#endif
