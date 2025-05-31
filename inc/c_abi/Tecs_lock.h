#pragma once

#include "Tecs_entity.h"
#include "Tecs_entity_view.h"
#include "Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#else
    #include <stdint.h>

typedef uint8_t bool;
#endif

#include <stddef.h>

typedef void tecs_lock_t;

TECS_EXPORT size_t Tecs_lock_get_transaction_id(tecs_lock_t *dynLockPtr);
TECS_EXPORT bool Tecs_lock_is_add_remove_allowed(tecs_lock_t *dynLockPtr);
TECS_EXPORT bool Tecs_lock_is_write_allowed(tecs_lock_t *dynLockPtr, size_t componentIndex);
TECS_EXPORT bool Tecs_lock_is_read_allowed(tecs_lock_t *dynLockPtr, size_t componentIndex);

TECS_EXPORT size_t Tecs_previous_entities_with(tecs_lock_t *dynLockPtr, size_t componentIndex,
    tecs_entity_view_t *output);
TECS_EXPORT size_t Tecs_entities_with(tecs_lock_t *dynLockPtr, size_t componentIndex, tecs_entity_view_t *output);
TECS_EXPORT size_t Tecs_previous_entities(tecs_lock_t *dynLockPtr, tecs_entity_view_t *output);
TECS_EXPORT size_t Tecs_entities(tecs_lock_t *dynLockPtr, tecs_entity_view_t *output);
TECS_EXPORT tecs_entity_t Tecs_new_entity(tecs_lock_t *dynLockPtr);
TECS_EXPORT bool Tecs_has(tecs_lock_t *dynLockPtr, size_t componentIndex);
TECS_EXPORT bool Tecs_had(tecs_lock_t *dynLockPtr, size_t componentIndex);
TECS_EXPORT const void *Tecs_const_get(tecs_lock_t *dynLockPtr, size_t componentIndex);
TECS_EXPORT void *Tecs_get(tecs_lock_t *dynLockPtr, size_t componentIndex);
TECS_EXPORT const void *Tecs_get_previous(tecs_lock_t *dynLockPtr, size_t componentIndex);
TECS_EXPORT void *Tecs_set(tecs_lock_t *dynLockPtr, size_t componentIndex, const void *value);
TECS_EXPORT void Tecs_unset(tecs_lock_t *dynLockPtr, size_t componentIndex);

// Observer<Event> Watch();
// void StopWatching(Observer<Event> observer);

// New lock must be released
TECS_EXPORT tecs_lock_t *Tecs_lock_read_only(tecs_lock_t *dynLockPtr);

#ifdef __cplusplus
} // extern "C"
#endif
