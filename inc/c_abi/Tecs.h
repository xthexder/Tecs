#pragma once

#include "Tecs_export.h"
#include "Tecs_lock.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef void tecs_ecs_t;

TECS_EXPORT tecs_ecs_t *Tecs_make_ecs_instance();
TECS_EXPORT void Tecs_release_ecs_instance(tecs_ecs_t *ecsPtr);

TECS_EXPORT tecs_lock_t *Tecs_ecs_start_transaction(tecs_ecs_t *ecsPtr, uint64_t readPermissions,
    uint64_t writePermissions);
TECS_EXPORT tecs_lock_t *Tecs_ecs_start_transaction_bitstr(tecs_ecs_t *ecsPtr, const char *readPermissions,
    const char *writePermissions);

TECS_EXPORT size_t Tecs_ecs_get_instance_id(tecs_ecs_t *ecsPtr);
TECS_EXPORT size_t Tecs_ecs_get_next_transaction_id(tecs_ecs_t *ecsPtr);
TECS_EXPORT size_t Tecs_ecs_get_component_count();
TECS_EXPORT size_t Tecs_ecs_get_component_size(size_t componentIndex);
TECS_EXPORT size_t Tecs_ecs_get_component_name(size_t componentIndex, size_t bufferSize, char *output);
TECS_EXPORT size_t Tecs_ecs_get_bytes_per_entity();

TECS_EXPORT void Tecs_lock_release(tecs_lock_t *dynLockPtr);

#ifdef __cplusplus
} // extern "C"
#endif
