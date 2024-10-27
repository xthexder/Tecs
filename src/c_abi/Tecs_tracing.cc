#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
        #define _CRT_SECURE_NO_WARNINGS
    #endif

    #include <Tecs.hh>
    #include <Tecs_tracing.hh>
    #include <c_abi/Tecs_tracing.h>
    #include <cstring>

    #ifdef TECS_C_ABI_ECS_INCLUDE
        #include TECS_C_ABI_ECS_INCLUDE
    #endif

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

void Tecs_perf_trace_set_thread_name(TecsPerfTrace *tracePtr, size_t threadIdHash, const char *threadName) {
    Tecs::PerformanceTrace *trace = static_cast<Tecs::PerformanceTrace *>(tracePtr);
    trace->SetThreadName(std::string(threadName), threadIdHash);
}

size_t Tecs_perf_trace_get_thread_name(TecsPerfTrace *tracePtr, size_t threadIdHash, size_t bufferSize, char *output) {
    Tecs::PerformanceTrace *trace = static_cast<Tecs::PerformanceTrace *>(tracePtr);
    std::string name = trace->GetThreadName(threadIdHash);
    if (name.size() < bufferSize) {
        (void)std::strncpy(output, name.c_str(), bufferSize);
    }
    return name.size() + 1;
}

void Tecs_perf_trace_save_to_csv(TecsPerfTrace *tracePtr, const char *filePath) {
    Tecs::PerformanceTrace *trace = static_cast<Tecs::PerformanceTrace *>(tracePtr);
    trace->SaveToCSV(std::string(filePath));
}

void Tecs_ecs_perf_trace_release(TecsPerfTrace *tracePtr) {
    Tecs::PerformanceTrace *trace = static_cast<Tecs::PerformanceTrace *>(tracePtr);
    delete trace;
}

} // extern "C"
#endif
