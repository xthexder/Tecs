#include "impl_gen_common.hh"

template<typename T>
void generateTracingCC(T &out) {
#ifdef TECS_C_ABI_ECS_INCLUDE
    out << "#include " STRINGIFY(TECS_C_ABI_ECS_INCLUDE) << std::endl;
#endif
    out << R"RAWSTR(
#include <Tecs.hh>
#include <Tecs_tracing.hh>
#include <c_abi/Tecs_tracing.h>

)RAWSTR";
    out << "using ECS = " << TypeToString<TECS_C_ABI_ECS_NAME>();
    out << R"RAWSTR(;

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
)RAWSTR";
}

int main(int argc, char **argv) {
    if (argc > 1) {
        auto out = std::ofstream(argv[1], std::ios::trunc);
        generateTracingCC(out);
    } else {
        generateTracingCC(std::cout);
    }
}
