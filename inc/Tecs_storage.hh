#pragma once

#include "Tecs_set_locks.hh"

#include <atomic>
#include <cstddef>
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
         * RUnlock() must be called exactly once after reading has completed.
         */
        inline void RLock() {
            int retry = 0;
            while (true) {
                uint32_t current = readers;
                if (writer != WRITER_COMMITING && current != READER_LOCKED) {
                    uint32_t next = current + 1;
                    if (readers.compare_exchange_weak(current, next)) {
                        // Lock aquired
                        return;
                    }
                }

                if (retry++ > TECS_SPINLOCK_RETRY_YIELD) {
                    retry = 0;
                    std::this_thread::yield();
                }
            }
        }

        /**
         * Unlock this Component type from a read lock. Behavior is undefined if RLock() is not called first.
         */
        inline void RUnlock() {
            readers--;
        }

        /**
         * Lock this Component type for writing. Only a single writer can be active at once. Readers are still allowed
         * to hold locks until the write is being committed.
         * This function will block if another writer has already started.
         *
         * CommitWrite() must be called exactly once after writing has completed.
         */
        inline void StartWrite() {
            int retry = 0;
            while (true) {
                uint32_t current = writer;
                if (current == WRITER_FREE) {
                    if (writer.compare_exchange_weak(current, WRITER_STARTED)) {
                        // Lock aquired
                        return;
                    }
                }

                if (retry++ > TECS_SPINLOCK_RETRY_YIELD) {
                    retry = 0;
                    std::this_thread::yield();
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
         * Behavior is undefined if StartWrite() is not called first.
         */
        template<bool AllowAddRemove>
        inline void CommitWrite() {
            int retry = 0;
            while (true) {
                uint32_t current = readers;
                if (current == READER_FREE) {
                    if (readers.compare_exchange_weak(current, READER_LOCKED)) {
                        // Lock aquired
                        commitEntities<AllowAddRemove>();
                        break;
                    }
                }

                if (retry++ > TECS_SPINLOCK_RETRY_YIELD) {
                    retry = 0;
                    std::this_thread::yield();
                }
            }

            // Unlock read and write copies
            readers = READER_FREE;
            writer = WRITER_FREE;
        }

    private:
        // Lock states
        static const uint32_t WRITER_FREE = 0;
        static const uint32_t WRITER_STARTED = 1;
        static const uint32_t WRITER_COMMITING = 2;
        static const uint32_t READER_FREE = 0;
        static const uint32_t READER_LOCKED = UINT32_MAX;

        std::atomic_uint32_t readers = 0;
        std::atomic_uint32_t writer = 0;

        /**
         * Copies the write buffer to the read buffer. This should only be called inside CommitWrite().
         * The valid index list is only copied if AllowAddRemove is true.
         */
        template<bool AllowAddRemove>
        inline void commitEntities() {
            if (AllowAddRemove) {
                // The number of components, or list of valid entities may have changed.
                readComponents = writeComponents;
                readValidEntities = writeValidEntities;
            } else {
                // Based on benchmarks, it is faster to bulk copy if more than roughly 1/6 of the components are valid.
                if (writeValidEntities.size() > writeComponents.size() / 6) {
                    readComponents = writeComponents;
                } else {
                    for (auto &valid : writeValidEntities) {
                        readComponents[valid.id] = writeComponents[valid.id];
                    }
                }
            }
        }

        std::vector<T> readComponents;
        std::vector<T> writeComponents;
        std::vector<Entity> readValidEntities;
        std::vector<Entity> writeValidEntities;

        template<typename, typename...>
        friend class ReadLockRef;
        template<typename, typename...>
        friend class ComponentWriteTransactionRef;
        template<typename>
        friend class EntityWriteTransactionRef;
    };
} // namespace Tecs
