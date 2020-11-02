#pragma once

#include "set_locks.hh"
#include "template_util.hh"

#include <atomic>
#include <bitset>
#include <cstddef>
#include <memory>
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

        inline void CommitWrite() {
            int retry = 0;
            while (true) {
                uint32_t current = readers;
                if (current == READER_FREE) {
                    if (readers.compare_exchange_weak(current, READER_LOCKED)) {
                        // Lock aquired
                        commitEntities();
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

        // MultiTimer timer = MultiTimer(std::string("ComponentIndex Commit ") + typeid(T).name());
        // MultiTimer timerDirty = MultiTimer(std::string("ComponentIndex CommitDirty ") + typeid(T).name());
        inline void commitEntities() {
            // TODO: Possibly make this a compile-time branch
            // writeValidDirty means there was a change to the list of valid components (i.e new entities were added)
            if (writeValidDirty) {
                // Timer t(timerDirty);
                readComponents = writeComponents;
                readValidIndexes = writeValidIndexes;
                writeValidDirty = false;
            } else {
                // Timer t(timer);
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
        bool writeValidDirty = false;

        template<typename, typename...>
        friend class ComponentSetReadLock;
        template<typename, bool, typename...>
        friend class ComponentSetWriteTransaction;
    };

    // ECS contains all ECS data. Component types must be known at compile-time and are passed in as
    // template arguments.
    template<typename... Tn>
    class ECS {
    public:
        template<typename... Un>
        inline ComponentSetReadLock<ECS<Tn...>, Un...> ReadEntitiesWith() {
            return ComponentSetReadLock<ECS<Tn...>, Un...>(*this);
        }

        template<bool AllowAddRemove, typename... Un>
        inline ComponentSetWriteTransaction<ECS<Tn...>, AllowAddRemove, Un...> ModifyEntitiesWith() {
            return ComponentSetWriteTransaction<ECS<Tn...>, AllowAddRemove, Un...>(*this);
        }

        template<typename U>
        inline static constexpr size_t GetIndex() {
            return GetIndex<0, U>();
        }

        inline static constexpr size_t GetComponentCount() {
            return sizeof...(Tn);
        }

    private:
        using ValidComponentSet = std::bitset<sizeof...(Tn)>;
        using IndexStorage = typename wrap_tuple_args<ComponentIndex, Tn...>::type;

        template<size_t I, typename U>
        inline static constexpr size_t GetIndex() {
            static_assert(I < sizeof...(Tn), "Component does not exist");

            if constexpr (std::is_same<U, typename std::tuple_element<I, std::tuple<Tn...>>::type>::value) {
                return I;
            } else {
                return GetIndex<I + 1, U>();
            }
        }

        template<typename U>
        inline static constexpr bool BitsetHas(ValidComponentSet &validBitset) {
            return validBitset[GetIndex<0, U>()];
        }

        template<typename U, typename U2, typename... Un>
        inline static constexpr bool BitsetHas(ValidComponentSet &validBitset) {
            return BitsetHas<U>(validBitset) && BitsetHas<U2, Un...>(validBitset);
        }

        template<typename T>
        inline constexpr ComponentIndex<T> &Storage() {
            return std::get<ComponentIndex<T>>(indexes);
        }

        ComponentIndex<ValidComponentSet> validIndex;
        IndexStorage indexes;

        template<typename, typename...>
        friend class ComponentSetReadLock;
        template<typename, bool, typename...>
        friend class ComponentSetWriteTransaction;
    };
} // namespace Tecs
