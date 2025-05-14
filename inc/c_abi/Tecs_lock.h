#pragma once

#include "Tecs_entity.h"
#include "Tecs_entity_view.h"
#include "Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#else
typedef uint8_t bool;
#endif

#include <stdint.h>

typedef void TecsLock;

TECS_EXPORT size_t Tecs_lock_get_transaction_id(TecsLock *dynLockPtr);
TECS_EXPORT bool Tecs_lock_is_add_remove_allowed(TecsLock *dynLockPtr);
TECS_EXPORT bool Tecs_lock_is_write_allowed(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT bool Tecs_lock_is_read_allowed(TecsLock *dynLockPtr, size_t componentIndex);

TECS_EXPORT size_t Tecs_previous_entities_with(TecsLock *dynLockPtr, size_t componentIndex, TecsEntityView *output);
TECS_EXPORT size_t Tecs_entities_with(TecsLock *dynLockPtr, size_t componentIndex, TecsEntityView *output);
TECS_EXPORT size_t Tecs_previous_entities(TecsLock *dynLockPtr, TecsEntityView *output);
TECS_EXPORT size_t Tecs_entities(TecsLock *dynLockPtr, TecsEntityView *output);
TECS_EXPORT TecsEntity Tecs_new_entity(TecsLock *dynLockPtr);
TECS_EXPORT bool Tecs_has(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT bool Tecs_had(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT const void *Tecs_const_get(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT void *Tecs_get(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT const void *Tecs_get_previous(TecsLock *dynLockPtr, size_t componentIndex);
TECS_EXPORT void *Tecs_set(TecsLock *dynLockPtr, size_t componentIndex, const void *value);
TECS_EXPORT void Tecs_unset(TecsLock *dynLockPtr, size_t componentIndex);

// Observer<Event> Watch();
// void StopWatching(Observer<Event> observer);

// New lock must be released
TECS_EXPORT TecsLock *Tecs_lock_read_only(TecsLock *dynLockPtr);

#ifdef __cplusplus
}
#endif
