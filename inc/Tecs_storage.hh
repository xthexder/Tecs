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
         * Lock this Component type for writing. Only a single writer can be active at once.
         * Readers are still allowed to hold locks until the write is being committed.
         * This function will block if another writer has already started.
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
         * Transition from a write lock to a commit lock so that changes made to this Component type can be copied from
         * the write buffer to the read buffer.
         *
         * Once this function is called, new reader locks will begin to block. When all existing readers have completed,
         * this function will unblock, ensuring this thread has exclusive access to both the read and write buffers.
         *
         * A commit lock can only be acquired once per write lock, and must be followed by a WriteUnlock().
         */
        inline void CommitLock() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            bool tracedWait = false;
#endif

            uint32_t current = writer;
            if (current != WRITER_LOCKED) {
                throw std::runtime_error("CommitLock called outside of WriteLock");
            } else if (!writer.compare_exchange_strong(current, WRITER_COMMIT)) {
                throw std::runtime_error("CommitLock writer changed unexpectedly");
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
         * Unlock readers and transition from a commit lock back to a write lock.
         * This should only be called between CommitLock() and WriteUnlock().
         */
        inline void CommitUnlock() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            traceInfo.Trace(TraceEvent::Type::CommitUnlock);
#endif

            // Unlock read copies immediately after commit completion
            uint32_t current = readers;
            if (current == READER_LOCKED) {
                if (!readers.compare_exchange_strong(current, READER_FREE)) {
                    throw std::runtime_error("CommitUnlock readers changed unexpectedly");
                }
            }
#if __cpp_lib_atomic_wait
            readers.notify_all();
#endif

            current = writer;
            if (current != WRITER_COMMIT) {
                throw std::runtime_error("CommitUnlock called outside of CommitLock");
            } else if (!writer.compare_exchange_strong(current, WRITER_LOCKED)) {
                throw std::runtime_error("CommitUnlock writer changed unexpectedly");
            }
#if __cpp_lib_atomic_wait
            writer.notify_all();
#endif
        }

        /**
         * Swaps the read and write buffers and copies any changes so the read and write buffers match.
         * This should only be called once between CommitLock() and WriteUnlock().
         * The valid entity list is only copied if AllowAddRemove is true.
         *
         * This function will automatically call CommitUnlock().
         */
        template<bool AllowAddRemove>
        inline void CommitEntities() {
            if (AllowAddRemove) {
                // The number of components, or list of valid entities may have changed.
                readComponents.swap(writeComponents);
                readValidEntities.swap(writeValidEntities);
                CommitUnlock();

                writeComponents = readComponents;
                writeValidEntities = readValidEntities;
            } else {
                readComponents.swap(writeComponents);
                CommitUnlock();

                // Based on benchmarks, it is faster to bulk copy if more than roughly 1/6 of the components are valid.
                if (readValidEntities.size() > writeComponents.size() / 6) {
                    writeComponents = readComponents;
                } else {
                    for (auto &valid : readValidEntities) {
                        writeComponents[valid.index] = readComponents[valid.index];
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
            } else if (!writer.compare_exchange_strong(current, WRITER_FREE)) {
                throw std::runtime_error("WriteUnlock writer changed unexpectedly");
            }
#if __cpp_lib_atomic_wait
            writer.notify_all();
#endif
        }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        TraceInfo traceInfo;
#endif

        inline static constexpr size_t GetBytesPerEntity() {
            return sizeof(T) * 2 + sizeof(Entity) * 2 + sizeof(size_t);
        }

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
