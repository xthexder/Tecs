#pragma once

#include "Tecs.h"
#include "Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef void tecs_perf_trace_t;

TECS_EXPORT void Tecs_ecs_start_perf_trace(tecs_ecs_t *ecsPtr);
TECS_EXPORT tecs_perf_trace_t *Tecs_ecs_stop_perf_trace(tecs_ecs_t *ecsPtr);
TECS_EXPORT void Tecs_perf_trace_set_thread_name(tecs_perf_trace_t *tracePtr, size_t threadIdHash,
    const char *threadName);
TECS_EXPORT size_t Tecs_perf_trace_get_thread_name(tecs_perf_trace_t *tracePtr, size_t threadIdHash, size_t bufferSize,
    char *output);
TECS_EXPORT void Tecs_perf_trace_save_to_csv(tecs_perf_trace_t *tracePtr, const char *filePath);
TECS_EXPORT void Tecs_ecs_perf_trace_release(tecs_perf_trace_t *tracePtr);

#ifdef __cplusplus
} // extern "C"
#endif
