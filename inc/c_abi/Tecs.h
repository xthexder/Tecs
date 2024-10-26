#pragma once

#include "Tecs_lock.h"
#ifdef __cplusplus
extern "C" {
#else
typedef uint8_t bool;
#endif

#include <stdint.h>

typedef void TecsECS;

TecsLock *Tecs_ecs_start_transaction(TecsECS *ecsPtr, unsigned long long readPermissions,
    unsigned long long writePermissions);
TecsLock *Tecs_ecs_start_transaction_bitstr(TecsECS *ecsPtr, const char *readPermissions, const char *writePermissions);

size_t Tecs_ecs_get_instance_id(TecsECS *ecsPtr);
size_t Tecs_ecs_get_component_count(TecsECS *ecsPtr);
size_t Tecs_ecs_get_component_name(TecsECS *ecsPtr, size_t componentIndex, size_t bufferSize, char *output);
size_t Tecs_ecs_get_bytes_per_entity(TecsECS *ecsPtr);

void Tecs_lock_release(TecsLock *dynLockPtr);

#ifdef __cplusplus
}
#endif
