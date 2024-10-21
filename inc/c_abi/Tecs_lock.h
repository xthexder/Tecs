#pragma once

#include "Tecs_entity.h"
#include "Tecs_entity_view.h"

#ifdef __cplusplus
extern "C" {
#else
typedef uint8_t bool;
#endif

#include <stdint.h>

typedef void TecsLock;

size_t Tecs_lock_get_transaction_id(TecsLock *dynLockPtr);
bool Tecs_lock_is_add_remove_allowed(TecsLock *dynLockPtr);
bool Tecs_lock_is_write_allowed(TecsLock *dynLockPtr, size_t componentIndex);
bool Tecs_lock_is_read_allowed(TecsLock *dynLockPtr, size_t componentIndex);

size_t Tecs_previous_entities_with(TecsLock *dynLockPtr, size_t componentIndex, TecsEntityView *output);
size_t Tecs_entities_with(TecsLock *dynLockPtr, size_t componentIndex, TecsEntityView *output);
size_t Tecs_previous_entities(TecsLock *dynLockPtr, TecsEntityView *output);
size_t Tecs_entities(TecsLock *dynLockPtr, TecsEntityView *output);
TecsEntity Tecs_new_entity(TecsLock *dynLockPtr);
bool Tecs_has(TecsLock *dynLockPtr, size_t componentIndex);
bool Tecs_had(TecsLock *dynLockPtr, size_t componentIndex);
const void *Tecs_const_get(TecsLock *dynLockPtr, size_t componentIndex);
void *Tecs_get(TecsLock *dynLockPtr, size_t componentIndex);
const void *Tecs_get_previous(TecsLock *dynLockPtr, size_t componentIndex);
void *Tecs_set(TecsLock *dynLockPtr, size_t componentIndex, const void *value);
void Tecs_unset(TecsLock *dynLockPtr, size_t componentIndex);

// Observer<Event> Watch();
// void StopWatching(Observer<Event> observer);

// New lock must be released
TecsLock *Tecs_lock_read_only(TecsLock *dynLockPtr);

#ifdef __cplusplus
}
#endif
