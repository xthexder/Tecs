#pragma once

#include "Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(bool) == 1, "Unexpected bool size");

typedef void tecs_lock_t;
typedef uint64_t tecs_entity_t;

TECS_EXPORT const void *Tecs_get_entity_storage(tecs_lock_t *dynLockPtr, uint32_t componentIndex);
TECS_EXPORT const void *Tecs_get_previous_entity_storage(tecs_lock_t *dynLockPtr, uint32_t componentIndex);

TECS_EXPORT bool Tecs_entity_exists(tecs_lock_t *dynLockPtr, tecs_entity_t entity);
TECS_EXPORT bool Tecs_entity_existed(tecs_lock_t *dynLockPtr, tecs_entity_t entity);
TECS_EXPORT bool Tecs_entity_has(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint32_t componentIndex);
TECS_EXPORT bool Tecs_entity_had(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint32_t componentIndex);
TECS_EXPORT bool Tecs_entity_has_bitset(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint64_t componentBits);
TECS_EXPORT bool Tecs_entity_had_bitset(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint64_t componentBits);
TECS_EXPORT const void *Tecs_entity_const_get(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint32_t componentIndex);
TECS_EXPORT void *Tecs_entity_get(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint32_t componentIndex);
TECS_EXPORT const void *Tecs_entity_get_previous(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint32_t componentIndex);
TECS_EXPORT void *Tecs_entity_set(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint32_t componentIndex,
    const void *value);
TECS_EXPORT void Tecs_entity_unset(tecs_lock_t *dynLockPtr, tecs_entity_t entity, uint32_t componentIndex);
TECS_EXPORT void Tecs_entity_destroy(tecs_lock_t *dynLockPtr, tecs_entity_t entity);

#ifdef __cplusplus
} // extern "C"
#endif
