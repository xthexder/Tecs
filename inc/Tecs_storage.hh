#pragma once

#include "Tecs_entity.hh"
#include "Tecs_observer.hh"
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #include "Tecs_tracing.hh"
#endif

#ifdef TECS_ENABLE_TRACY
    #include <tracy/Tracy.hpp>
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
#ifdef TECS_ENABLE_TRACY
            bool runAfterLockShared = false;
            if (block) {
                runAfterLockShared = tracyRead.BeforeLockShared();
            }
#endif

            int retry = 0;
            while (true) {
                uint32_t currentReaders = readers;
                uint32_t currentWriter = writer;
                if (currentReaders != READER_LOCKED && currentWriter != WRITER_COMMIT) {
                    uint32_t next = currentReaders + 1;
                    if (readers.compare_exchange_weak(currentReaders,
                            next,
                            std::memory_order_acquire,
                            std::memory_order_relaxed)) {
                        // Lock aquired
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                        traceInfo.Trace(TraceEvent::Type::ReadLock);
#endif
#ifdef TECS_ENABLE_TRACY
                        if (block) {
                            if (runAfterLockShared) tracyRead.AfterLockShared();
                        } else {
                            tracyRead.AfterTryLockShared(true);
                        }
#endif
                        return true;
                    }
                }

                if (!block) {
#ifdef TECS_ENABLE_TRACY
                    tracyRead.AfterTryLockShared(false);
#endif
                    return false;
                }

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
            readers.fetch_sub(1, std::memory_order_release);
#if __cpp_lib_atomic_wait
            readers.notify_all();
#endif
#ifdef TECS_ENABLE_TRACY
            tracyRead.AfterUnlockShared();
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
#ifdef TECS_ENABLE_TRACY
            bool runAfterLock = false;
            if (block) {
                runAfterLock = tracyWrite.BeforeLock();
            }
#endif

            int retry = 0;
            while (true) {
                uint32_t current = writer;
                if (current == WRITER_FREE) {
                    if (writer.compare_exchange_weak(current,
                            WRITER_LOCKED,
                            std::memory_order_acquire,
                            std::memory_order_relaxed)) {
                        // Lock aquired
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                        traceInfo.Trace(TraceEvent::Type::WriteLock);
#endif
#ifdef TECS_ENABLE_TRACY
                        if (block) {
                            if (runAfterLock) tracyWrite.AfterLock();
                        } else {
                            tracyWrite.AfterTryLock(true);
                        }
#endif
                        return true;
                    }
                }

                if (!block) {
#ifdef TECS_ENABLE_TRACY
                    tracyWrite.AfterTryLock(false);
#endif
                    return false;
                }

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
#ifdef TECS_ENABLE_TRACY
            bool runAfterLock = tracyRead.BeforeLock();
#endif

            uint32_t current = writer;
            if (current != WRITER_LOCKED) {
                throw std::runtime_error("CommitLock called outside of WriteLock");
            } else if (!writer.compare_exchange_strong(current, WRITER_COMMIT, std::memory_order_acquire)) {
                throw std::runtime_error("CommitLock writer changed unexpectedly");
            }

            int retry = 0;
            while (true) {
                current = readers;
                if (current == READER_FREE) {
                    if (readers.compare_exchange_weak(current,
                            READER_LOCKED,
                            std::memory_order_acquire,
                            std::memory_order_relaxed)) {
                        // Lock aquired
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
                        traceInfo.Trace(TraceEvent::Type::CommitLock);
#endif
#ifdef TECS_ENABLE_TRACY
                        if (runAfterLock) tracyRead.AfterLock();
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
            if (current != READER_LOCKED) {
                throw std::runtime_error("CommitUnlock called outside of CommitLock");
            } else if (!readers.compare_exchange_strong(current, READER_FREE, std::memory_order_release)) {
                throw std::runtime_error("CommitUnlock readers changed unexpectedly");
            }
#if __cpp_lib_atomic_wait
            readers.notify_all();
#endif

            current = writer;
            if (current != WRITER_COMMIT) {
                throw std::runtime_error("CommitUnlock called outside of CommitLock");
            } else if (!writer.compare_exchange_strong(current, WRITER_LOCKED, std::memory_order_release)) {
                throw std::runtime_error("CommitUnlock writer changed unexpectedly");
            }
#if __cpp_lib_atomic_wait
            writer.notify_all();
#endif
#ifdef TECS_ENABLE_TRACY
            tracyRead.AfterUnlock();
#endif
        }

        inline void WriteUnlock() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            traceInfo.Trace(TraceEvent::Type::WriteUnlock);
#endif

            // Release exclusive read lock if a commit lock is held
            uint32_t current = readers;
            if (current == READER_LOCKED) {
                if (!readers.compare_exchange_strong(current, READER_FREE, std::memory_order_release)) {
                    throw std::runtime_error("WriteUnlock readers changed unexpectedly");
                }
#if __cpp_lib_atomic_wait
                readers.notify_all();
#endif
            }

            // Relase write lock
            current = writer;
            if (current != WRITER_LOCKED && current != WRITER_COMMIT) {
                throw std::runtime_error("WriteUnlock called outside of WriteLock");
            } else if (!writer.compare_exchange_strong(current, WRITER_FREE, std::memory_order_release)) {
                throw std::runtime_error("WriteUnlock writer changed unexpectedly");
            }
#if __cpp_lib_atomic_wait
            writer.notify_all();
#endif

#ifdef TECS_ENABLE_TRACY
            tracyWrite.AfterUnlock();
#endif
        }

        // Transitions between a write lock acquired state to a read lock acquired state.
        // Read locks cannot transitioned to write locks without starting a new transaction.
        inline void WriteToReadLock() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            traceInfo.Trace(TraceEvent::Type::WriteToReadLock);
#endif

            // Acquire a shared read lock, releasing the exclusive read lock if a commit lock is held
            uint32_t current = readers;
            if (current == READER_LOCKED) {
                if (!readers.compare_exchange_strong(current, READER_FREE + 1, std::memory_order_release)) {
                    throw std::runtime_error("WriteToReadLock readers changed unexpectedly");
                }
            } else {
                current = readers.fetch_add(1, std::memory_order_acquire);
                if (current == READER_LOCKED) {
                    throw std::runtime_error("WriteToReadLock read lock acquire conflict");
                }
            }
#if __cpp_lib_atomic_wait
            readers.notify_all();
#endif
#ifdef TECS_ENABLE_TRACY
            tracyRead.AfterTryLockShared(true);
#endif

            // Release write lock
            current = writer;
            if (current != WRITER_LOCKED && current != WRITER_COMMIT) {
                throw std::runtime_error("WriteToReadLock called outside of WriteLock");
            } else if (!writer.compare_exchange_strong(current, WRITER_FREE, std::memory_order_release)) {
                throw std::runtime_error("WriteToReadLock writer changed unexpectedly");
            }
#if __cpp_lib_atomic_wait
            writer.notify_all();
#endif

#ifdef TECS_ENABLE_TRACY
            tracyWrite.AfterUnlock();
#endif
        }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        TraceInfo traceInfo;
#endif
#ifdef TECS_ENABLE_TRACY
        static inline const auto tracyReadCtx = []() -> const tracy::SourceLocationData * {
            static const std::string lockName = "Read "s + typeid(T *).name();
            static const tracy::SourceLocationData srcloc{nullptr, lockName.c_str(), __FILE__, __LINE__, 0};
            return &srcloc;
        };
        static inline const auto tracyWriteCtx = []() -> const tracy::SourceLocationData * {
            static const std::string lockName = "Write "s + typeid(T *).name();
            static const tracy::SourceLocationData srcloc{nullptr, lockName.c_str(), __FILE__, __LINE__, 0};
            return &srcloc;
        };
        tracy::SharedLockableCtx tracyRead{tracyReadCtx()};
        tracy::LockableCtx tracyWrite{tracyWriteCtx()};
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
        template<typename>
        friend class Transaction;
        friend struct Entity;
    };
} // namespace Tecs
