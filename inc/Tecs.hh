#pragma once

#include "set_locks.hh"
#include "storage.hh"
#include "template_util.hh"

#include <bitset>
#include <cstddef>

namespace Tecs {
    // ECS contains all ECS data. Component types must be known at compile-time and are passed in as
    // template arguments.
    template<typename... Tn>
    class ECS {
    public:
        template<typename... Un>
        inline ReadLock<ECS<Tn...>, Un...> ReadEntitiesWith() {
            return ReadLock<ECS<Tn...>, Un...>(*this);
        }

        template<typename... Un>
        inline ComponentWriteTransaction<ECS<Tn...>, Un...> WriteEntitiesWith() {
            return ComponentWriteTransaction<ECS<Tn...>, Un...>(*this);
        }

        inline EntityWriteTransaction<ECS<Tn...>> AddRemoveEntities() {
            return EntityWriteTransaction<ECS<Tn...>>(*this);
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
        friend class ReadLock;
        template<typename, typename...>
        friend class ComponentWriteTransaction;
        template<typename>
        friend class EntityWriteTransaction;
    };
} // namespace Tecs
