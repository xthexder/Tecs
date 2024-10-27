#pragma once

#include "Tecs_permissions.hh"
#include "nonstd/span.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <ostream>
#include <sstream>
#include <thread>
#include <vector>

#ifndef TECS_PERFORMANCE_TRACING_MAX_EVENTS
    #define TECS_PERFORMANCE_TRACING_MAX_EVENTS 10000
#endif

namespace Tecs {
    struct TraceEvent {
        enum class Type {
            Invalid = 0,
            TransactionStart,
            TransactionEnd,
            ReadLockWait,
            ReadLock,
            ReadUnlock,
            WriteLockWait,
            WriteLock,
            CommitLockWait,
            CommitLock,
            CommitUnlock,
            WriteUnlock,
        };

        Type type = Type::Invalid;
        std::thread::id thread;
        std::chrono::steady_clock::time_point time;
    };

    static inline std::ostream &operator<<(std::ostream &out, const TraceEvent::Type &t) {
        static const std::array eventTypeNames = {
            "Invalid",
            "TransactionStart",
            "TransactionEnd",
            "ReadLockWait",
            "ReadLock",
            "ReadUnlock",
            "WriteLockWait",
            "WriteLock",
            "CommitLockWait",
            "CommitLock",
            "CommitUnlock",
            "WriteUnlock",
        };
        return out << eventTypeNames[(size_t)t];
    }

    struct PerformanceTrace {
        nonstd::span<TraceEvent> transactionEvents;
        nonstd::span<TraceEvent> metadataEvents;
        std::vector<nonstd::span<TraceEvent>> componentEvents;
        std::vector<std::string> componentNames;
        std::map<size_t, std::string> threadNames;

        void SetThreadName(const std::string &name, size_t threadIdHash) {
            threadNames[threadIdHash] = name;
        }

        void SetThreadName(const std::string &name, std::thread::id threadId = std::this_thread::get_id()) {
            static std::hash<std::thread::id> threadHasher;
            SetThreadName(name, threadHasher(threadId));
        }

        std::string GetThreadName(size_t threadIdHash) {
            auto it = threadNames.find(threadIdHash);
            if (it != threadNames.end()) {
                return it->second;
            }
            std::stringstream ss;
            ss << threadIdHash;
            return ss.str();
        }

        std::string GetThreadName(std::thread::id threadId = std::this_thread::get_id()) {
            static std::hash<std::thread::id> threadHasher;
            auto it = threadNames.find(threadHasher(threadId));
            if (it != threadNames.end()) {
                return it->second;
            }
            std::stringstream ss;
            ss << threadId;
            return ss.str();
        }

        void SaveToCSV(const std::string &filePath) {
            std::ofstream out(filePath);
            out << "Transaction Event,Transaction Thread Id,Transaction TimeNs";
            out << ",Metadata Event,Metadata Thread Id,Metadata TimeNs";
            if (componentEvents.size() != componentNames.size()) {
                throw std::runtime_error("Trying to save a trace with mismatched array sizes");
            }
            for (size_t i = 0; i < componentEvents.size(); i++) {
                auto &name = componentNames[i];
                out << "," << name << " Event," << name << " Thread Id," << name << " TimeNs";
            }
            out << std::endl;

            bool done = false;
            for (size_t row = 0; !done; row++) {
                done = true;

                if (row < transactionEvents.size()) {
                    auto &event = transactionEvents[row];
                    out << event.type << "," << GetThreadName(event.thread) << ",";
                    out << std::chrono::duration_cast<std::chrono::nanoseconds>(event.time.time_since_epoch()).count();
                    done = false;
                } else {
                    out << ",,";
                }

                if (row < metadataEvents.size()) {
                    auto &event = metadataEvents[row];
                    out << "," << event.type << "," << GetThreadName(event.thread) << ",";
                    out << std::chrono::duration_cast<std::chrono::nanoseconds>(event.time.time_since_epoch()).count();
                    done = false;
                } else {
                    out << ",,,";
                }

                for (auto &events : componentEvents) {
                    if (row < events.size()) {
                        auto &event = events[row];
                        out << "," << event.type << "," << GetThreadName(event.thread) << ",";
                        out << std::chrono::duration_cast<std::chrono::nanoseconds>(event.time.time_since_epoch())
                                   .count();
                        done = false;
                    } else {
                        out << ",,,";
                    }
                }
                out << std::endl;
            }
            out.close();
        }
    };

    class TraceInfo {
    public:
        TraceInfo() : traceEnabled(false), nextEventIndex(0), events(TECS_PERFORMANCE_TRACING_MAX_EVENTS) {}

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
            return nonstd::span<TraceEvent>(events.data(), std::min(events.size(), (size_t)nextEventIndex.load()));
        }

    private:
        std::atomic_bool traceEnabled;
        std::atomic_uint32_t nextEventIndex;
        std::vector<TraceEvent> events;
    };
} // namespace Tecs
