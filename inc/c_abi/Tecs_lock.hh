#pragma once

#include "../Tecs_permissions.hh"
#include "Tecs_entity_view.h"
#include "Tecs_lock.h"

namespace Tecs::abi {
    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... Permissions>
    class Lock<ECSType<AllComponentTypes...>, Permissions...> {
    private:
        using ECS = ECSType<AllComponentTypes...>;
        using LockType = Lock<ECS, Permissions...>;

        std::shared_ptr<TecsLock> base;

    public:
        inline Lock(const std::shared_ptr<TecsLock> &lock) : base(lock) {
            if constexpr (is_add_remove_allowed<LockType>()) {
                if (!Tecs_lock_is_add_remove_allowed(lock.get())) {
                    throw std::runtime_error("Lock is missing AddRemove permissions");
                }
            } else {
                size_t componentIndex = 0;
                (
                    [&] {
                        if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
                            if (!Tecs_lock_is_write_allowed(lock.get(), componentIndex)) {
                                throw std::runtime_error("Lock does not have " +
                                                         std::string(typeid(AllComponentTypes).name()) +
                                                         " write permissions");
                            }
                        }
                        if constexpr (is_read_allowed<AllComponentTypes, LockType>()) {
                            if (!Tecs_lock_is_read_allowed(lock.get(), componentIndex)) {
                                throw std::runtime_error("Lock does not have " +
                                                         std::string(typeid(AllComponentTypes).name()) +
                                                         " read permissions");
                            }
                        }
                        componentIndex++;
                    }(),
                    ...);
            }
        }

        // Returns true if this lock type can be constructed from a lock with the specified source permissions
        template<typename... PermissionsSource>
        static constexpr bool is_lock_subset() {
            using SourceLockType = Lock<ECS, PermissionsSource...>;
            if constexpr (is_add_remove_allowed<LockType>() && !is_add_remove_allowed<SourceLockType>()) {
                return false;
            } else {
                return std::conjunction<Tecs::is_lock_subset<AllComponentTypes, LockType, SourceLockType>...>();
            }
        }

        // Returns true if this lock type has all of the requested permissions
        template<typename... RequestedPermissions>
        static constexpr bool has_permissions() {
            return Lock<ECS, RequestedPermissions...>::template is_lock_subset<LockType>();
        }

        // Reference an existing transaction
        template<typename... PermissionsSource, std::enable_if_t<is_lock_subset<PermissionsSource...>(), int> = 0>
        inline Lock(const Lock<ECS, PermissionsSource...> &source) : base(source.base) {}

        inline size_t GetTransactionId() const {
            return Tecs_lock_get_transaction_id(base.get());
        }

        template<typename T>
        inline const EntityView PreviousEntitiesWith() const {
            static_assert(!is_global_component<T>(), "Entities can't have global components");

            constexpr size_t componentIndex = ECS::template GetComponentIndex<T>();
            TecsEntityView view = {};
            (void)Tecs_previous_entities_with(base.get(), componentIndex, &view);
            return EntityView(view);
        }

        template<typename T>
        inline const EntityView EntitiesWith() const {
            static_assert(!is_global_component<T>(), "Entities can't have global components");

            constexpr size_t componentIndex = ECS::template GetComponentIndex<T>();
            TecsEntityView view = {};
            (void)Tecs_entities_with(base.get(), componentIndex, &view);
            return EntityView(view);
        }

        inline const EntityView PreviousEntities() const {
            TecsEntityView view = {};
            (void)Tecs_previous_entities(base.get(), &view);
            return EntityView(view);
        }

        inline const EntityView Entities() const {
            TecsEntityView view = {};
            (void)Tecs_entities(base.get(), &view);
            return EntityView(view);
        }

        /**
         * Creates a new entity with AddRemove lock permissions.
         *
         * Note: This function invalidates all references to components if a storage resize occurs.
         */
        inline Entity NewEntity() const {
            static_assert(is_add_remove_allowed<LockType>(), "Lock does not have AddRemove permission.");

            return Entity(Tecs_new_entity(base.get()));
        }

        template<typename... Tn>
        inline bool Has() const {
            static_assert(all_global_components<Tn...>(), "Only global components can be accessed without an Entity");

            return (Tecs_has(base.get(), ECS::template GetComponentIndex<Tn>()) && ...);
        }

        template<typename... Tn>
        inline bool Had() const {
            static_assert(all_global_components<Tn...>(), "Only global components can be accessed without an Entity");

            return (Tecs_had(base.get(), ECS::template GetComponentIndex<Tn>()) && ...);
        }

        template<typename T, typename ReturnType =
                                 std::conditional_t<is_write_allowed<std::remove_cv_t<T>, LockType>::value, T, const T>>
        inline ReturnType &Get() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(is_write_allowed<CompType, LockType>() || std::is_const<ReturnType>(),
                "Can't get non-const reference of read only Component.");
            static_assert(is_global_component<CompType>(), "Only global components can be accessed without an Entity");

            constexpr size_t componentIndex = ECS::template GetComponentIndex<CompType>();
            if constexpr (std::is_const<ReturnType>()) {
                return *static_cast<const CompType *>(Tecs_const_get(base.get(), componentIndex));
            } else {
                return *static_cast<CompType *>(Tecs_get(base.get(), componentIndex));
            }
        }

        template<typename T>
        inline const T &GetPrevious() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(is_global_component<CompType>(), "Only global components can be accessed without an Entity");

            constexpr size_t componentIndex = ECS::template GetComponentIndex<CompType>();
            return *static_cast<const CompType *>(Tecs_get_previous(base.get(), componentIndex));
        }

        template<typename T>
        inline T &Set(T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");

            constexpr size_t componentIndex = ECS::template GetComponentIndex<T>();
            return *static_cast<T *>(Tecs_set(base.get(), componentIndex, &value));
        }

        template<typename T, typename... Args>
        inline T &Set(Args &&...args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");

            T temp = T(std::forward<Args>(args)...);

            constexpr size_t componentIndex = ECS::template GetComponentIndex<T>();
            return *static_cast<T *>(Tecs_set(base.get(), componentIndex, &temp));
        }

        template<typename... Tn>
        inline void Unset() const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");
            static_assert(all_global_components<Tn...>(), "Only global components can be unset without an Entity");

            (Tecs_unset(base.get(), ECS::template GetComponentIndex<Tn>()), ...);
        }

        // template<typename Event>
        // inline Observer<ECS, Event> Watch() const {
        //     static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to watch for ecs
        //     changes.");

        //     auto &observerList = instance.template Observers<Event>();
        //     auto &eventList = observerList.observers.emplace_back(std::make_shared<std::deque<Event>>());
        //     return Observer(instance, eventList);
        // }

        // template<typename Event>
        // inline void StopWatching(Observer<ECS, Event> &observer) const {
        //     static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to stop an observer.");
        //     auto eventList = observer.eventListWeak.lock();
        //     auto &observers = instance.template Observers<Event>().observers;
        //     observers.erase(std::remove(observers.begin(), observers.end(), eventList), observers.end());
        //     observer.eventListWeak.reset();
        // }

        // template<typename... PermissionsSubset>
        // inline Lock<ECS, PermissionsSubset...> Subset() const {
        //     using NewLockType = Lock<ECS, PermissionsSubset...>;
        //     static_assert(has_permissions<NewLockType>(), "Lock types are not a subset of existing permissions.");

        //     return NewLockType(*this);
        // }

        /**
         * Convert this lock into a read-only variant.
         *
         * Reads performed through this lock will not be able to see writes from the parent lock, and instead will
         * return the previous value.
         */
        // inline auto ReadOnlySubset() const {
        //     using NewLockType = Lock<ECS, typename FlattenPermissions<LockType,
        //     AllComponentTypes...>::type_readonly>; static_assert(has_permissions<NewLockType>(), "Lock types are not
        //     a subset of existing permissions."); return NewLockType(this->instance, this->base, {});
        // }

        // long UseCount() const {
        //     return base.use_count();
        // }

    private:
        friend struct Entity;
    };
} // namespace Tecs::abi
