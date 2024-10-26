#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #ifdef TECS_C_ABI_ECS_INCLUDE
        #include TECS_C_ABI_ECS_INCLUDE
    #endif

    #include <Tecs.hh>
    #include <Tecs_tracing.hh>
    #include <c_abi/Tecs_tracing.h>

    #ifdef TECS_C_ABI_ECS_NAME
using ECS = TECS_C_ABI_ECS_NAME;
    #else
using ECS = Tecs::ECS<>;
    #endif

extern "C" {

void Tecs_ecs_start_perf_trace(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    ecs->StartTrace();
}

TecsPerfTrace *Tecs_ecs_stop_perf_trace(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return new Tecs::PerformanceTrace(ecs->StopTrace());
}

void Tecs_perf_trace_set_thread_name(TecsPerfTrace *tracePtr, size_t thread_id_hash, const char *thread_name) {
    Tecs::PerformanceTrace *trace = static_cast<Tecs::PerformanceTrace *>(tracePtr);
    trace->SetThreadName(std::string(thread_name), thread_id_hash);
}

const char *Tecs_perf_trace_get_thread_name(TecsPerfTrace *tracePtr, size_t thread_id_hash) {
    Tecs::PerformanceTrace *trace = static_cast<Tecs::PerformanceTrace *>(tracePtr);
    return trace->GetThreadName(thread_id_hash).c_str();
}

void Tecs_perf_trace_save_to_csv(TecsPerfTrace *tracePtr, const char *file_path) {
    Tecs::PerformanceTrace *trace = static_cast<Tecs::PerformanceTrace *>(tracePtr);
    trace->SaveToCSV(std::string(file_path));
}

void Tecs_ecs_perf_trace_release(TecsPerfTrace *tracePtr) {
    Tecs::PerformanceTrace *trace = static_cast<Tecs::PerformanceTrace *>(tracePtr);
    delete trace;
}

} // extern "C"
#endif
