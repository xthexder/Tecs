#pragma once

#include "Tecs_tracing.h"

#include <memory>
#include <string>
#include <thread>

namespace Tecs::abi {
    struct PerformanceTrace {
        std::shared_ptr<TecsPerfTrace> base;

        void SetThreadName(const std::string &name, size_t threadIdHash) {
            Tecs_perf_trace_set_thread_name(base.get(), threadIdHash, name.c_str());
        }

        void SetThreadName(const std::string &name, std::thread::id threadId = std::this_thread::get_id()) {
            static const std::hash<std::thread::id> threadHasher;
            SetThreadName(name, threadHasher(threadId));
        }

        std::string GetThreadName(size_t threadIdHash) {
            size_t size = Tecs_perf_trace_get_thread_name(base.get(), threadIdHash, 0, nullptr);
            std::string str(size, '\0');
            Tecs_perf_trace_get_thread_name(base.get(), threadIdHash, size, str.data());
            return str;
        }

        std::string GetThreadName(std::thread::id threadId = std::this_thread::get_id()) {
            static const std::hash<std::thread::id> threadHasher;
            return GetThreadName(threadHasher(threadId));
        }

        void SaveToCSV(const std::string &filePath) {
            Tecs_perf_trace_save_to_csv(base.get(), filePath.c_str());
        }
    };
} // namespace Tecs::abi
