#pragma once

#include "Tecs_export.h"
#include "Tecs_lock.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef void TecsECS;

TECS_EXPORT TecsECS *Tecs_make_ecs_instance();
TECS_EXPORT void Tecs_release_ecs_instance(TecsECS *ecsPtr);

TECS_EXPORT TecsLock *Tecs_ecs_start_transaction(TecsECS *ecsPtr, uint64_t readPermissions, uint64_t writePermissions);
TECS_EXPORT TecsLock *Tecs_ecs_start_transaction_bitstr(TecsECS *ecsPtr, const char *readPermissions,
    const char *writePermissions);

TECS_EXPORT size_t Tecs_ecs_get_instance_id(TecsECS *ecsPtr);
TECS_EXPORT size_t Tecs_ecs_get_next_transaction_id(TecsECS *ecsPtr);
TECS_EXPORT size_t Tecs_ecs_get_component_count(TecsECS *ecsPtr);
TECS_EXPORT size_t Tecs_ecs_get_component_name(TecsECS *ecsPtr, size_t componentIndex, size_t bufferSize, char *output);
TECS_EXPORT size_t Tecs_ecs_get_bytes_per_entity(TecsECS *ecsPtr);

TECS_EXPORT void Tecs_lock_release(TecsLock *dynLockPtr);

#ifdef __cplusplus
}
#endif
