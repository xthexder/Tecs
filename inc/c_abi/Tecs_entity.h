#pragma once

#include "Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#else
typedef uint8_t bool;
#endif

#include <stdint.h>

typedef void TecsLock;
typedef uint64_t TecsEntity;

TECS_EXPORT bool Tecs_entity_exists(TecsLock *dynLockPtr, TecsEntity entity);
TECS_EXPORT bool Tecs_entity_existed(TecsLock *dynLockPtr, TecsEntity entity);
TECS_EXPORT bool Tecs_entity_has(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
TECS_EXPORT bool Tecs_entity_had(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
TECS_EXPORT bool Tecs_entity_has_bitset(TecsLock *dynLockPtr, TecsEntity entity, unsigned long long componentBits);
TECS_EXPORT bool Tecs_entity_had_bitset(TecsLock *dynLockPtr, TecsEntity entity, unsigned long long componentBits);
TECS_EXPORT const void *Tecs_entity_const_get(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
TECS_EXPORT const void *Tecs_const_get_entity_storage(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT void *Tecs_entity_get(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
TECS_EXPORT void *Tecs_get_entity_storage(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT const void *Tecs_entity_get_previous(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
TECS_EXPORT const void *Tecs_get_previous_entity_storage(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT void *Tecs_entity_set(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex, const void *value);
TECS_EXPORT void Tecs_entity_unset(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
TECS_EXPORT void Tecs_entity_destroy(TecsLock *dynLockPtr, TecsEntity entity);

#ifdef __cplusplus
}
#endif
