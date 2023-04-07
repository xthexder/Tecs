#pragma once

#include "Tecs_entity.hh"
#include "Tecs_lock.hh"
#include "Tecs_permissions.hh"
#include "Tecs_storage.hh"
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #include "Tecs_tracing.hh"
#endif

#include <bitset>
#include <cstddef>
#include <deque>
#include <tuple>
#include <type_traits>
#include <vector>

namespace Tecs {
    /**
     * An ECS "world" is created by instantiating this class. Component types must be known at compile-time and are
     * passed in as template arguments.
     *
     * All operations are done through Transactions with Read, Write, or AddRemove permissions.
     * These transactions are thread-safe, and can be run simultaniously from multiple threads with no additional
     * external synchronization.
     *
     * It is recommended to instantiate an ECS in a static place in memory so it can be easily accessed from multiple
     * threads.
     */
    template<typename... Tn>
    class ECS {
    public:
        ECS() {
#ifndef TECS_HEADER_ONLY
            ecsId = ++nextEcsId;
#endif
        }

        /**
         * Start a new transaction with a specific set of permissions, and return a Lock object.
         *
         * Permissions can be any combination of the following:
         * Tecs::Read<Components...>     - Allow read-only access to a list of Component types
         * Tecs::ReadAll                 - Allow read-only access to all existing Components
         * Tecs::Write<Components...>    - Allow write access to a list of Component types (existing Components only)
         * Tecs::WriteAll                - Allow write access to all existing Components
         * Tecs::AddRemove               - Allow the creation and deletion of new Entities and Components
         *
         * It is recommended to start transactions with the minimum required permissions to prevent unnecessary thread
         * synchronization. Only a single transaction should be active in a single thread at a time; nesting
         * transactions is undefined behavior and may result in deadlocks.
         *
         * All data access must be done through the returned Lock object, or by passing the lock to an Entity function.
         */
        template<typename... Permissions>
        inline Lock<ECS<Tn...>, Permissions...> StartTransaction() {
            return Lock<ECS<Tn...>, Permissions...>(*this);
        }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        void StartTrace() {
            transactionTrace.StartTrace();
            metadata.traceInfo.StartTrace();
            (Storage<Tn>().traceInfo.StartTrace(), ...);
        }

        PerformanceTrace StopTrace() {
            return PerformanceTrace{
                transactionTrace.StopTrace(),             // transactionEvents
                metadata.traceInfo.StopTrace(),           // metadataEvents
                {Storage<Tn>().traceInfo.StopTrace()...}, // componentEvents
                {GetComponentName<Tn>()...},              // componentNames
                {},                                       // threadNames
            };
        }
#endif

        inline TECS_ENTITY_ECS_IDENTIFIER_TYPE GetInstanceId() const {
            return (TECS_ENTITY_ECS_IDENTIFIER_TYPE)ecsId;
        }

        /**
         * Returns the index of a Component type for use in a bitset.
         */
        template<typename U>
        static constexpr size_t GetComponentIndex() {
            return GetComponentIndex<0, U>();
        }

        /**
         * Returns the number of Component types registered in this ECS instance.
         */
        static constexpr size_t GetComponentCount() {
            return sizeof...(Tn);
        }

        /**
         * Returns the registered name of a Component type, or a default of "ComponentN" if none is set.
         */
        template<typename U>
        static std::string GetComponentName() {
            if constexpr (std::extent<decltype(component_name<U>::value)>::value > 1) {
                return component_name<U>::value;
            } else {
                return "Component" + std::to_string(GetComponentIndex<U>());
            }
        }

        /**
         * Returns true if the Component type is part of this ECS.
         */
        template<typename U>
        static constexpr bool IsComponent() {
            return contains<U, Tn...>();
        }

        static constexpr size_t GetBytesPerEntity() {
            return (ComponentIndex<Tn>::GetBytesPerEntity() + ...);
        }

    private:
        template<typename Event>
        struct ObserverList {
            std::vector<std::shared_ptr<std::deque<Event>>> observers;
            std::shared_ptr<std::deque<Event>> writeQueue;

            void Init() {
                if (!writeQueue) writeQueue = std::make_shared<std::deque<Event>>();
            }

            void Commit() {
                for (auto &observer : observers) {
                    observer->insert(observer->end(), writeQueue->begin(), writeQueue->end());
                }
                writeQueue->clear();
            }
        };

        template<size_t I, typename U>
        inline static constexpr size_t GetComponentIndex() {
            static_assert(I < sizeof...(Tn), "Component does not exist");

            if constexpr (std::is_same<U, typename std::tuple_element<I, std::tuple<Tn...>>::type>::value) {
                return I;
            } else {
                return GetComponentIndex<I + 1, U>();
            }
        }

        using ComponentBitset = std::bitset<1 + sizeof...(Tn)>;

        struct EntityMetadata : public ComponentBitset {
            TECS_ENTITY_GENERATION_TYPE generation = 0;
        };

        template<typename... Un>
        inline static constexpr bool BitsetHas(const ComponentBitset &bitset) {
            return (bitset[1 + GetComponentIndex<0, Un>()] && ...);
        }

        template<typename T>
        inline constexpr ComponentIndex<T> &Storage() {
            static_assert(contains<T, Tn...>(), "Component is not registered with Tecs");
            return std::get<ComponentIndex<T>>(indexes);
        }

        template<typename Event>
        inline constexpr ObserverList<Event> &Observers() {
            static_assert(contains<Event, EntityEvent, ComponentEvent<Tn>...>(), "Event is not registered with Tecs");
            return std::get<ObserverList<Event>>(eventLists);
        }

        ComponentIndex<EntityMetadata> metadata;
        ComponentBitset globalReadMetadata;
        ComponentBitset globalWriteMetadata;
        std::tuple<ComponentIndex<Tn>...> indexes;
        std::deque<Entity> freeEntities;

        std::tuple<ObserverList<EntityEvent>, ObserverList<ComponentEvent<Tn>>...> eventLists;

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        TraceInfo transactionTrace;
#endif

#ifndef TECS_HEADER_ONLY
        size_t ecsId;
#endif

        template<typename, typename...>
        friend class Lock;
        template<typename, typename...>
        friend class Transaction;
        template<template<typename...> typename, typename...>
        friend class BaseTransaction;
        friend struct Entity;
    };
} // namespace Tecs
