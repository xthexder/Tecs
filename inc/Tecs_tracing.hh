#pragma once

#include "nonstd/span.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

#ifndef TECS_PERFORMANCE_TRACING_MAX_EVENTS
    #define TECS_PERFORMANCE_TRACING_MAX_EVENTS 10000
#endif

namespace Tecs {
    struct TraceEvent {
        enum class Type {
            Invalid,
            ReadLockWait,
            ReadLock,
            ReadUnlock,
            WriteLockWait,
            WriteLock,
            CommitLockWait,
            CommitLock,
            WriteUnlock
        };

        Type type = Type::Invalid;
        std::thread::id thread;
        std::chrono::steady_clock::time_point time;
    };

    class TraceInfo {
    public:
        TraceInfo() : traceEnabled(false), nextEventIndex(0) {}

        inline void Trace(TraceEvent::Type eventType) {
            if (traceEnabled) {
                auto index = nextEventIndex++;
                if (index < events.size()) {
                    auto &event = events[index];
                    event.time = std::chrono::steady_clock::now();
                    event.thread = std::this_thread::get_id();
                    event.type = eventType;
                }
            }
        }

        inline void StartTrace() {
            if (traceEnabled) throw std::runtime_error("An existing trace has already started");
            nextEventIndex = 0;
            traceEnabled = true;
        }

        inline nonstd::span<TraceEvent> StopTrace() {
            if (!traceEnabled) throw std::runtime_error("No trace has been started");
            traceEnabled = false;
            return nonstd::span<TraceEvent>(events.begin(), nextEventIndex);
        }

    private:
        std::atomic_bool traceEnabled;
        std::atomic_uint32_t nextEventIndex;
        std::array<TraceEvent, TECS_PERFORMANCE_TRACING_MAX_EVENTS> events;
    };
} // namespace Tecs
