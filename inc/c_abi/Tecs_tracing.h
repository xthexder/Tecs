#pragma once

#include "Tecs.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef void TecsPerfTrace;

void Tecs_ecs_start_perf_trace(TecsECS *ecsPtr);
TecsPerfTrace *Tecs_ecs_stop_perf_trace(TecsECS *ecsPtr);
void Tecs_perf_trace_set_thread_name(TecsPerfTrace *tracePtr, size_t thread_id_hash, const char *thread_name);
const char *Tecs_perf_trace_get_thread_name(TecsPerfTrace *tracePtr, size_t thread_id_hash);
void Tecs_perf_trace_save_to_csv(TecsPerfTrace *tracePtr, const char *file_path);
void Tecs_ecs_perf_trace_release(TecsPerfTrace *tracePtr);

#ifdef __cplusplus
}
#endif
