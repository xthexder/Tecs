#include "Tecs_storage.hh"

#include <thread>

static_assert(ATOMIC_INT_LOCK_FREE == 2, "std::atomic_int is not lock-free");

namespace Tecs {
    /**
     * Lock this Component type for reading. Multiple readers can hold this lock at once.
     * This function will only block if a writer is in the process of commiting.
     *
     * ReadUnlock() must be called exactly once after reading has completed.
     */
    bool ComponentMutex::ReadLock(bool block) {
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
    void ComponentMutex::ReadUnlock() {
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
    bool ComponentMutex::WriteLock(bool block) {
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
    void ComponentMutex::CommitLock() {
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
    void ComponentMutex::CommitUnlock() {
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

    void ComponentMutex::WriteUnlock() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        traceInfo.Trace(TraceEvent::Type::WriteUnlock);
#endif

        // Unlock read and write copies
        uint32_t current = readers;
        if (current == READER_LOCKED) {
            if (!readers.compare_exchange_strong(current, READER_FREE, std::memory_order_release)) {
                throw std::runtime_error("WriteUnlock readers changed unexpectedly");
            }
        }
#if __cpp_lib_atomic_wait
        readers.notify_all();
#endif

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
} // namespace Tecs
