#pragma once

#include "Tecs.h"
#include "Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef void TecsPerfTrace;

TECS_EXPORT void Tecs_ecs_start_perf_trace(TecsECS *ecsPtr);
TECS_EXPORT TecsPerfTrace *Tecs_ecs_stop_perf_trace(TecsECS *ecsPtr);
TECS_EXPORT void Tecs_perf_trace_set_thread_name(TecsPerfTrace *tracePtr, size_t threadIdHash, const char *threadName);
TECS_EXPORT size_t Tecs_perf_trace_get_thread_name(TecsPerfTrace *tracePtr, size_t threadIdHash, size_t bufferSize,
    char *output);
TECS_EXPORT void Tecs_perf_trace_save_to_csv(TecsPerfTrace *tracePtr, const char *filePath);
TECS_EXPORT void Tecs_ecs_perf_trace_release(TecsPerfTrace *tracePtr);

#ifdef __cplusplus
}
#endif
