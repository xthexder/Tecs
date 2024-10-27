#pragma once

#include "Tecs.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef void TecsPerfTrace;

void Tecs_ecs_start_perf_trace(TecsECS *ecsPtr);
TecsPerfTrace *Tecs_ecs_stop_perf_trace(TecsECS *ecsPtr);
void Tecs_perf_trace_set_thread_name(TecsPerfTrace *tracePtr, size_t threadIdHash, const char *threadName);
size_t Tecs_perf_trace_get_thread_name(TecsPerfTrace *tracePtr, size_t threadIdHash, size_t bufferSize, char *output);
void Tecs_perf_trace_save_to_csv(TecsPerfTrace *tracePtr, const char *filePath);
void Tecs_ecs_perf_trace_release(TecsPerfTrace *tracePtr);

#ifdef __cplusplus
}
#endif
