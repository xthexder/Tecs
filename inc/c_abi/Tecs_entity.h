#pragma once

#ifdef __cplusplus
extern "C" {
#else
typedef uint8_t bool;
#endif

#include "Tecs_lock.h"

#include <stdint.h>

typedef uint64_t TecsEntity;

bool Tecs_entity_exists(TecsLock *dynLockPtr, TecsEntity entity);
bool Tecs_entity_existed(TecsLock *dynLockPtr, TecsEntity entity);
bool Tecs_entity_has(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
bool Tecs_entity_had(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
const void *Tecs_entity_const_get(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
void *Tecs_entity_get(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
const void *Tecs_entity_get_previous(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
void *Tecs_entity_set(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex, const void *value);
void Tecs_entity_unset(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex);
void Tecs_entity_destroy(TecsLock *dynLockPtr, TecsEntity entity);

#ifdef __cplusplus
}
#endif
