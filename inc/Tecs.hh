#pragma once

#include "Tecs_set_locks.hh"
#include "Tecs_storage.hh"
#include "Tecs_template_util.hh"

#include <bitset>
#include <cstddef>

namespace Tecs {
    /**
     * An ECS "world" is created by instantiating this class. Component types must be known at compile-time and are
     * passed in as template arguments.
     *
     * All operations are done through one of three types of Transactions: Read, WriteComponents, and WriteEntities
     * These transactions are thread-safe, and can be run simultaniously from multiple threads with no additional
     * external synchronization.
     *
     * It is recommended to instantiate an ECS in a static place in memory so it can be easily accessed from multiple
     * threads.
     */
    template<typename... Tn>
    class ECS {
    public:
        /**
         * Lock a set of Component types to allow read-only access.
         * Multiple read locks can be held at once.
         *
         * The lock is held until the returned handle is deconstructed.
         * Values read through the returned handle will remain constant.
         */
        template<typename... Un>
        inline ReadLock<ECS<Tn...>, Un...> ReadEntitiesWith() {
            // return ReadLock<ECS<Tn...>, Un...>(*this);
            return {*this};
        }

        /**
         * Lock a set of Component types to allow write access. This only allows changes to existing components.
         * Only a single write lock can be held at once per Component type, but non-overlapping writes can occur
         * simultaneously.
         *
         * The lock is held until the returned handle is deconstructed.
         */
        template<typename... Un>
        inline WriteLock<ECS<Tn...>, Un...> WriteEntitiesWith() {
            // return WriteLock<ECS<Tn...>, Un...>(*this);
            return {*this};
        }

        /**
         * Lock all entities to allow adding / removing of entities and components.
         * Only a instance of this lock can be held at once.
         *
         * The lock is held until the returned handle is deconstructed.
         */
        inline AddRemoveLock<ECS<Tn...>> AddRemoveEntities() {
            // return AddRemoveLock<ECS<Tn...>>(*this);
            return {*this};
        }

        /**
         * Returns the index of a Component type for use in a bitset.
         */
        template<typename U>
        inline static constexpr size_t GetComponentIndex() {
            return GetComponentIndex<0, U>();
        }

        /**
         * Returns the number of Component types registered in this ECS instance.
         */
        inline static constexpr size_t GetComponentCount() {
            return sizeof...(Tn);
        }

    private:
        using ValidComponentSet = std::bitset<sizeof...(Tn)>;
        using IndexStorage = typename wrap_tuple_args<ComponentIndex, Tn...>::type;

        template<size_t I, typename U>
        inline static constexpr size_t GetComponentIndex() {
            static_assert(I < sizeof...(Tn), "Component does not exist");

            if constexpr (std::is_same<U, typename std::tuple_element<I, std::tuple<Tn...>>::type>::value) {
                return I;
            } else {
                return GetComponentIndex<I + 1, U>();
            }
        }

        template<typename U>
        inline static constexpr bool BitsetHas(ValidComponentSet &validBitset) {
            return validBitset[GetComponentIndex<0, U>()];
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
        friend class ReadLockRef;
        template<typename, typename...>
        friend class ReadLock;
        template<typename, typename...>
        friend class WriteLockRef;
        template<typename, typename...>
        friend class WriteLock;
        template<typename>
        friend class AddRemoveLockRef;
        template<typename>
        friend class AddRemoveLock;
    };
} // namespace Tecs
