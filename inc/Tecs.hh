#pragma once

#include "Tecs_storage.hh"
#include "Tecs_template_util.hh"
#include "Tecs_transaction.hh"

#include <bitset>
#include <cstddef>
#include <deque>

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
        template<typename... Permissions>
        inline Transaction<ECS<Tn...>, Permissions...> StartTransaction() {
            return Transaction<ECS<Tn...>, Permissions...>(*this);
        }

        // TODO: Rewrite me for StartTransaction
        /**
         * Lock a set of Component types to allow read-only access.
         * Multiple read locks can be held at once.
         *
         * The lock is held until the returned handle is deconstructed.
         * Values read through the returned handle will remain constant.
         */
        /**
         * Lock a set of Component types to allow write access. This only allows changes to existing components.
         * Only a single write lock can be held at once per Component type, but non-overlapping writes can occur
         * simultaneously.
         *
         * The lock is held until the returned handle is deconstructed.
         */
        /**
         * Lock all entities to allow adding / removing of entities and components.
         * Only a instance of this lock can be held at once.
         *
         * The lock is held until the returned handle is deconstructed.
         */

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
        using ValidBitset = std::bitset<1 + sizeof...(Tn)>;
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
        inline static constexpr bool BitsetHas(const ValidBitset &validBitset) {
            return validBitset[1 + GetComponentIndex<0, U>()];
        }

        template<typename U, typename U2, typename... Un>
        inline static constexpr bool BitsetHas(const ValidBitset &validBitset) {
            return BitsetHas<U>(validBitset) && BitsetHas<U2, Un...>(validBitset);
        }

        template<typename T>
        inline constexpr ComponentIndex<T> &Storage() {
            return std::get<ComponentIndex<T>>(indexes);
        }

        ComponentIndex<ValidBitset> validIndex;
        IndexStorage indexes;
        std::deque<Entity> freeEntities;

        template<typename, typename...>
        friend class Lock;
        template<typename, typename...>
        friend class Transaction;
    };
} // namespace Tecs
