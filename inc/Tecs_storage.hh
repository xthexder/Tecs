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
#include <vector>

#ifndef TECS_SPINLOCK_RETRY_YIELD
    #define TECS_SPINLOCK_RETRY_YIELD 10
#endif

namespace Tecs {
    class ComponentMutex {
    private:
        // Lock states
        static const uint32_t WRITER_FREE = 0;
        static const uint32_t WRITER_LOCKED = 1;
        static const uint32_t WRITER_COMMIT = 2;
        static const uint32_t READER_FREE = 0;
        static const uint32_t READER_LOCKED = UINT32_MAX;

        std::atomic_uint32_t readers = 0;
        std::atomic_uint32_t writer = 0;

#ifdef TECS_ENABLE_TRACY
        tracy::SharedLockableCtx tracyRead;
        tracy::LockableCtx tracyWrite;
#endif

    public:
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        TraceInfo traceInfo;
#endif

#ifdef TECS_ENABLE_TRACY
        ComponentMutex(const tracy::SourceLocationData *tracyReadCtx, const tracy::SourceLocationData *tracyWriteCtx)
            : tracyRead(tracyReadCtx), tracyWrite(tracyWriteCtx) {}
#else
        ComponentMutex() {}
#endif

        /**
         * Lock this Component type for reading. Multiple readers can hold this lock at once.
         * This function will only block if a writer is in the process of commiting.
         *
         * ReadUnlock() must be called exactly once after reading has completed.
         */
        bool ReadLock(bool block = true);

        /**
         * Unlock this Component type from a read lock. Behavior is undefined if ReadLock() is not called first.
         */
        void ReadUnlock();

        /**
         * Lock this Component type for writing. Only a single writer can be active at once.
         * Readers are still allowed to hold locks until the write is being committed.
         * This function will block if another writer has already started.
         */
        bool WriteLock(bool block = true);

        /**
         * Transition from a write lock to a commit lock so that changes made to this Component type can be copied from
         * the write buffer to the read buffer.
         *
         * Once this function is called, new reader locks will begin to block. When all existing readers have completed,
         * this function will unblock, ensuring this thread has exclusive access to both the read and write buffers.
         *
         * A commit lock can only be acquired once per write lock, and must be followed by a WriteUnlock().
         */
        void CommitLock();

        /**
         * Unlock readers and transition from a commit lock back to a write lock.
         * This should only be called between CommitLock() and WriteUnlock().
         */
        void CommitUnlock();

        void WriteUnlock();
    };

    template<typename T>
    class ComponentIndex : public ComponentMutex {
    public:
#ifdef TECS_ENABLE_TRACY
        static inline const auto tracyReadCtx = []() -> const tracy::SourceLocationData * {
            static const std::string lockName = std::string("Read ") + typeid(T *).name();
            static const tracy::SourceLocationData srcloc{nullptr, lockName.c_str(), __FILE__, __LINE__, 0};
            return &srcloc;
        };
        static inline const auto tracyWriteCtx = []() -> const tracy::SourceLocationData * {
            static const std::string lockName = std::string("Write ") + typeid(T *).name();
            static const tracy::SourceLocationData srcloc{nullptr, lockName.c_str(), __FILE__, __LINE__, 0};
            return &srcloc;
        };

        ComponentIndex() : ComponentMutex(tracyReadCtx(), tracyWriteCtx()) {}
#endif

        inline static constexpr size_t GetBytesPerEntity() {
            return sizeof(T) * 2 + sizeof(Entity) * 2 + sizeof(size_t);
        }

    private:
        std::vector<T> readComponents;
        std::vector<T> writeComponents;
        std::vector<Entity> readValidEntities;
        std::vector<Entity> writeValidEntities;
        std::vector<size_t> validEntityIndexes; // Indexes into writeValidEntities

        template<typename, typename>
        friend class LockImpl;
        template<typename>
        friend class Transaction;
        friend struct Entity;
    };
} // namespace Tecs
