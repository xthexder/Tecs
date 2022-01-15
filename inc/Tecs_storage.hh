#pragma once

#include "Tecs_entity.hh"
#include "Tecs_observer.hh"
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #include "Tecs_tracing.hh"
#endif

#include <atomic>
#include <cstddef>
#include <set>
#include <thread>
#include <vector>

#ifndef TECS_SPINLOCK_RETRY_YIELD
    #define TECS_SPINLOCK_RETRY_YIELD 10
#endif

static_assert(ATOMIC_INT_LOCK_FREE == 2, "std::atomic_int is not lock-free");

namespace Tecs {
    template<typename T>
    class ComponentIndex {
    public:
        /**
         * Lock this Component type for reading. Multiple readers can hold this lock at once.
         * This function will only block if a writer is in the process of commiting.
         *
         * ReadUnlock() must be called exactly once after reading has completed.
         */
        inline bool ReadLock(bool block = true) {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            bool tracedWait = false;
#endif

            int retry = 0;
            while (true) {
                uint32_t currentReaders = readers;
                uint32_t currentWriter = writer;
                if (currentReaders != READER_LOCKED && currentWriter != WRITER_COMMIT) {
                    uint32_t next = currentReaders + 1;
                    if (readers.compare_exchange_weak(currentReaders, next)) {
                        // Lock aquired
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                        traceInfo.Trace(TraceEvent::Type::ReadLock);
#endif
                        return true;
                    }
                }

                if (!block) return false;

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                if (!tracedWait) {
                    traceInfo.Trace(TraceEvent::Type::ReadLockWait);
                    tracedWait = true;
                }
#endif

                if (retry++ > TECS_SPINLOCK_RETRY_YIELD) {
                    retry = 0;
#if __cpp_lib_atomic_wait
                    if (currentWriter == WRITER_COMMIT) {
                        writer.wait(currentWriter);
                    } else if (currentReaders == READER_LOCKED) {
                        readers.wait(currentReaders);
                    }
#else
                    std::this_thread::yield();
#endif
                }
            }
        }

        /**
         * Unlock this Component type from a read lock. Behavior is undefined if ReadLock() is not called first.
         */
        inline void ReadUnlock() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            traceInfo.Trace(TraceEvent::Type::ReadUnlock);
#endif

            uint32_t current = readers;
            if (current == READER_FREE || current == READER_LOCKED) {
                throw std::runtime_error("ReadUnlock called outside of ReadLock");
            }
            readers--;
#if __cpp_lib_atomic_wait
            readers.notify_all();
#endif
        }

        /**
         * Lock this Component type for writing. Only a single writer can be active at once. Readers are still allowed
         * to hold locks until the write is being committed.
         * This function will block if another writer has already started.
         *
         * Once writing has completed CommitLock() must be called exactly once, followed by WriteUnlock().
         */
        inline bool WriteLock(bool block = true) {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            bool tracedWait = false;
#endif

            int retry = 0;
            while (true) {
                uint32_t current = writer;
                if (current == WRITER_FREE) {
                    if (writer.compare_exchange_weak(current, WRITER_LOCKED)) {
                        // Lock aquired
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                        traceInfo.Trace(TraceEvent::Type::WriteLock);
#endif
                        return true;
                    }
                }

                if (!block) return false;

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                if (!tracedWait) {
                    traceInfo.Trace(TraceEvent::Type::WriteLockWait);
                    tracedWait = true;
                }
#endif

                if (retry++ > TECS_SPINLOCK_RETRY_YIELD) {
                    retry = 0;
#if __cpp_lib_atomic_wait
                    if (current != WRITER_FREE) writer.wait(current);
#else
                    std::this_thread::yield();
#endif
                }
            }
        }

        /**
         * Commit changes made to this Component type so that readers will begin to see the written values.
         *
         * Once this function is called, new reader locks will begin to block. When all existing readers have completed,
         * the changes are applied to the reader buffer.
         *
         * Once the commit is complete, both reader and writer locks will be free.
         * Behavior is undefined if WriteLock() is not called first.
         */
        inline void CommitLock() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            bool tracedWait = false;
#endif

            uint32_t current = writer;
            if (current != WRITER_LOCKED) {
                throw std::runtime_error("CommitLock called outside of WriteLock");
            } else {
                if (!writer.compare_exchange_strong(current, WRITER_COMMIT)) {
                    throw std::runtime_error("CommitLock writer changed unexpectedly");
                }
            }

            int retry = 0;
            while (true) {
                current = readers;
                if (current == READER_FREE) {
                    if (readers.compare_exchange_weak(current, READER_LOCKED)) {
                        // Lock aquired
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                        traceInfo.Trace(TraceEvent::Type::CommitLock);
#endif
                        return;
                    }
                }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                if (!tracedWait) {
                    traceInfo.Trace(TraceEvent::Type::CommitLockWait);
                    tracedWait = true;
                }
#endif

                if (retry++ > TECS_SPINLOCK_RETRY_YIELD) {
                    retry = 0;
#if __cpp_lib_atomic_wait
                    if (current != READER_FREE) readers.wait(current);
#else
                    std::this_thread::yield();
#endif
                }
            }
        }

        /**
         * Copies the write buffer to the read buffer. This should only be called between CommitLock() and
         * WriteUnlock(). The valid entity list is only copied if AllowAddRemove is true.
         */
        template<bool AllowAddRemove>
        inline void CommitEntities() {
            if (AllowAddRemove) {
                // The number of components, or list of valid entities may have changed.
                readComponents = writeComponents;
                readValidEntities = writeValidEntities;
            } else {
                // Based on benchmarks, it is faster to bulk copy if more than roughly 1/6 of the components are valid.
                if (readValidEntities.size() > writeComponents.size() / 6) {
                    readComponents = writeComponents;
                } else {
                    for (auto &valid : readValidEntities) {
                        readComponents[valid.index] = writeComponents[valid.index];
                    }
                }
            }
        }

        inline void WriteUnlock() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            traceInfo.Trace(TraceEvent::Type::WriteUnlock);
#endif

            // Unlock read and write copies
            uint32_t current = readers;
            if (current == READER_LOCKED) {
                if (!readers.compare_exchange_strong(current, READER_FREE)) {
                    throw std::runtime_error("WriteUnlock readers changed unexpectedly");
                }
            }
#if __cpp_lib_atomic_wait
            readers.notify_all();
#endif

            current = writer;
            if (current != WRITER_LOCKED && current != WRITER_COMMIT) {
                throw std::runtime_error("WriteUnlock called outside of WriteLock");
            }
            if (!writer.compare_exchange_strong(current, WRITER_FREE)) {
                throw std::runtime_error("WriteUnlock writer changed unexpectedly");
            }
#if __cpp_lib_atomic_wait
            writer.notify_all();
#endif
        }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        TraceInfo traceInfo;
#endif

    private:
        // Lock states
        static const uint32_t WRITER_FREE = 0;
        static const uint32_t WRITER_LOCKED = 1;
        static const uint32_t WRITER_COMMIT = 2;
        static const uint32_t READER_FREE = 0;
        static const uint32_t READER_LOCKED = UINT32_MAX;

        std::atomic_uint32_t readers = 0;
        std::atomic_uint32_t writer = 0;

        std::vector<T> readComponents;
        std::vector<T> writeComponents;
        std::vector<Entity> readValidEntities;
        std::vector<Entity> writeValidEntities;
        std::vector<size_t> validEntityIndexes; // Indexes into writeValidEntities

        template<typename, typename...>
        friend class Lock;
        template<typename, typename...>
        friend class Transaction;
        friend struct Entity;
    };
} // namespace Tecs
