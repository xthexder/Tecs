#pragma once

#include "Tecs.h"
#include "Tecs_lock.hh"
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #include "Tecs_tracing.hh"
#endif

#include <bitset>

namespace Tecs::abi {
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
    private:
        std::shared_ptr<TecsECS> base;

    public:
        inline ECS(const std::shared_ptr<TecsECS> &ecs) : base(ecs) {
            auto count = GetComponentCount();
            auto baseCount = Tecs_ecs_get_component_count(base.get());
            if (count != baseCount) {
                throw std::runtime_error("Component count missmatch: count " + std::to_string(count) + " != base " +
                                         std::to_string(baseCount));
            }
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
            using LockType = Lock<ECS<Tn...>, Permissions...>;

            std::bitset<1 + sizeof...(Tn)> readPermissions;
            std::bitset<1 + sizeof...(Tn)> writePermissions;
            readPermissions[0] = true;
            writePermissions[0] = is_add_remove_allowed<LockType>();
            // clang-format off
            ((
                readPermissions[1 + GetComponentIndex<Tn>()] = is_read_allowed<Tn, LockType>(),
                writePermissions[1 + GetComponentIndex<Tn>()] = is_write_allowed<Tn, LockType>()
            ), ...);
            // clang-format on
            TecsLock *l = nullptr;
            if constexpr (sizeof...(Tn) <= std::numeric_limits<unsigned long long>::digits) {
                l = Tecs_ecs_start_transaction(base.get(), readPermissions.to_ullong(), writePermissions.to_ullong());
            } else {
                l = Tecs_ecs_start_transaction_bitstr(base.get(),
                    readPermissions.to_string().c_str(),
                    writePermissions.to_string().c_str());
            }
            std::shared_ptr<TecsLock> lockPtr(l, [](auto *ptr) {
                Tecs_lock_release(ptr);
            });
            return LockType(lockPtr);
        }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        inline void StartTrace() {
            Tecs_ecs_start_perf_trace(base.get());
        }

        inline PerformanceTrace StopTrace() {
            TecsPerfTrace *trace = Tecs_ecs_stop_perf_trace(base.get());
            return PerformanceTrace{std::shared_ptr<TecsPerfTrace>(trace, [](auto *ptr) {
                Tecs_ecs_perf_trace_release(ptr);
            })};
        }
#endif

        inline TECS_ENTITY_ECS_IDENTIFIER_TYPE GetInstanceId() const {
            return (TECS_ENTITY_ECS_IDENTIFIER_TYPE)Tecs_ecs_get_instance_id(base.get());
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

        /**
         * Returns the registered name of the Nth Component type, or a default of "ComponentN" if none is set.
         */
        inline std::string GetComponentName(size_t componentIndex) {
            size_t size = Tecs_ecs_get_component_name(base.get(), componentIndex, 0, nullptr);
            std::string str(size, '\0');
            Tecs_ecs_get_component_name(base.get(), componentIndex, size, str.data());
            return str;
        }

        /**
         * Returns the registered name of a Component type, or a default of "ComponentN" if none is set.
         */
        template<typename U>
        inline std::string GetComponentName() {
            return GetComponentName(GetComponentIndex<U>());
        }

        /**
         * Returns true if the Component type is part of this ECS.
         */
        template<typename U>
        inline static constexpr bool IsComponent() {
            return contains<U, Tn...>();
        }

        inline size_t GetBytesPerEntity() {
            return Tecs_ecs_get_bytes_per_entity(base.get());
        }

    private:
        template<size_t I, typename U>
        inline static constexpr size_t GetComponentIndex() {
            static_assert(I < sizeof...(Tn), "Component does not exist");

            if constexpr (std::is_same<U, typename std::tuple_element<I, std::tuple<Tn...>>::type>::value) {
                return I;
            } else {
                return GetComponentIndex<I + 1, U>();
            }
        }
    };
} // namespace Tecs::abi
