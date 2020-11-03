#pragma once

#include "set_locks.hh"

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

        inline void RUnlock() {
            readers--;
        }

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
        static const uint32_t WRITER_FREE = 0;
        static const uint32_t WRITER_STARTED = 1;
        static const uint32_t WRITER_COMMITING = 2;
        static const uint32_t READER_FREE = 0;
        static const uint32_t READER_LOCKED = UINT32_MAX;

        std::atomic_uint32_t readers = 0;
        std::atomic_uint32_t writer = 0;

        template<bool AllowAddRemove>
        inline void commitEntities() {
            if (AllowAddRemove) {
                // The number of components, or list of valid indexes may have changed.
                readComponents = writeComponents;
                readValidIndexes = writeValidIndexes;
            } else {
                // Based on benchmarks, it is faster to bulk copy if more than roughly 1/6 of the components are valid.
                if (writeValidIndexes.size() > writeComponents.size() / 6) {
                    readComponents = writeComponents;
                } else {
                    for (auto &valid : writeValidIndexes) {
                        readComponents[valid] = writeComponents[valid];
                    }
                }
            }
        }

        std::vector<T> readComponents;
        std::vector<T> writeComponents;
        std::vector<size_t> readValidIndexes;
        std::vector<size_t> writeValidIndexes;

        template<typename, typename...>
        friend class ReadLock;
        template<typename, typename...>
        friend class ComponentWriteTransaction;
        template<typename>
        friend class EntityWriteTransaction;
    };
} // namespace Tecs
