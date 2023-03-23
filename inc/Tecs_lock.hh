#pragma once

#include "Tecs_entity.hh"
#include "Tecs_entity_view.hh"
#include "Tecs_observer.hh"
#include "Tecs_permissions.hh"
#include "Tecs_transaction.hh"

#include <bitset>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

#ifndef TECS_ENTITY_ALLOCATION_BATCH_SIZE
    #define TECS_ENTITY_ALLOCATION_BATCH_SIZE 1000
#endif

static_assert(TECS_ENTITY_ALLOCATION_BATCH_SIZE > 0, "At least 1 entity needs to be allocated at once.");

namespace Tecs {
    template<typename, typename...>
    class DynamicLock;

    /**
     * Lock<ECS, Permissions...> is a reference to lock permissions held by an active Transaction.
     *
     * Permissions... can be any combination of the following:
     * Tecs::Read<Components...>
     * Tecs::ReadAll
     * Tecs::Write<Components...>
     * Tecs::WriteAll
     * Tecs::AddRemove
     *
     * // Examples:
     * using ECSType = Tecs::ECS<A, B, C>;
     *
     * // A lock allowing read access to A and B, as well as write access to C.
     * Lock<ECSType, Read<A, B>, Write<C>> lock1;
     *
     * // A Lock allowing the creation and removal of entities and components.
     * Lock<ECSType, AddRemove> lock2;
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... Permissions>
    class Lock<ECSType<AllComponentTypes...>, Permissions...> {
    private:
        using ECS = ECSType<AllComponentTypes...>;
        using LockType = Lock<ECS, Permissions...>;

        ECS &instance;
        std::shared_ptr<BaseTransaction<ECSType, AllComponentTypes...>> base;
        std::bitset<1 + sizeof...(AllComponentTypes)> permissions;

        // Private constructor for DynamicLock to Lock conversion
        template<typename... PermissionsSource>
        inline Lock(ECS &instance, decltype(base) base, decltype(permissions) permissions)
            : instance(instance), base(base), permissions(permissions) {}

    public:
        // Start a new transaction
        inline Lock(ECS &instance) : instance(instance) {
            base = std::make_shared<Transaction<ECS, Permissions...>>(instance);
            permissions[0] = is_add_remove_allowed<LockType>();
            // clang-format off
            ((
                permissions[1 + instance.template GetComponentIndex<AllComponentTypes>()] = is_write_allowed<AllComponentTypes, LockType>()
            ), ...);
            // clang-format on
        }

        template<typename... PermissionsSource>
        static constexpr bool has_all_permissions() {
            using SourceLockType = Lock<ECS, PermissionsSource...>;
            if constexpr (is_add_remove_allowed<LockType>() && !is_add_remove_allowed<SourceLockType>()) {
                return false;
            } else {
                return std::conjunction<is_lock_subset<AllComponentTypes, LockType, SourceLockType>...>();
            }
        }

        // Reference an existing transaction
        template<typename... PermissionsSource, std::enable_if_t<has_all_permissions<PermissionsSource...>(), int> = 0>
        inline Lock(const Lock<ECS, PermissionsSource...> &source)
            : instance(source.instance), base(source.base), permissions(source.permissions) {}

        inline constexpr ECS &GetInstance() const {
            return instance;
        }

        template<typename T>
        inline const EntityView PreviousEntitiesWith() const {
            static_assert(!is_global_component<T>(), "Entities can't have global components");

            return instance.template Storage<T>().readValidEntities;
        }

        template<typename T>
        inline const EntityView EntitiesWith() const {
            static_assert(!is_global_component<T>(), "Entities can't have global components");

            if (permissions[0]) {
                return instance.template Storage<T>().writeValidEntities;
            } else {
                return instance.template Storage<T>().readValidEntities;
            }
        }

        inline const EntityView PreviousEntities() const {
            return instance.metadata.readValidEntities;
        }

        inline const EntityView Entities() const {
            if (permissions[0]) {
                return instance.metadata.writeValidEntities;
            } else {
                return instance.metadata.readValidEntities;
            }
        }

        /**
         * Creates a new entity with AddRemove lock permissions.
         *
         * Note: This function invalidates all references to components if a storage resize occurs.
         */
        inline Entity NewEntity() const {
            static_assert(is_add_remove_allowed<LockType>(), "Lock does not have AddRemove permission.");
            base->writeAccessedFlags[0] = true;

            Entity entity;
            if (instance.freeEntities.empty()) {
                // Allocate a new set of entities and components
                (AllocateComponents<AllComponentTypes>(TECS_ENTITY_ALLOCATION_BATCH_SIZE), ...);
                size_t nextIndex = instance.metadata.writeComponents.size();
                size_t newSize = nextIndex + TECS_ENTITY_ALLOCATION_BATCH_SIZE;
                if (newSize > std::numeric_limits<TECS_ENTITY_INDEX_TYPE>::max()) {
                    throw std::runtime_error("New entity index overflows type: " + std::to_string(newSize));
                }
                instance.metadata.writeComponents.resize(newSize);
                instance.metadata.validEntityIndexes.resize(newSize);

                // Add all but 1 of the new Entity ids to the free list.
                for (size_t count = 1; count < TECS_ENTITY_ALLOCATION_BATCH_SIZE; count++) {
                    instance.freeEntities.emplace_back((TECS_ENTITY_INDEX_TYPE)(nextIndex + count),
                        1,
                        (TECS_ENTITY_ECS_IDENTIFIER_TYPE)instance.ecsId);
                }
                entity = Entity((TECS_ENTITY_INDEX_TYPE)nextIndex, 1, (TECS_ENTITY_ECS_IDENTIFIER_TYPE)instance.ecsId);
            } else {
                entity = instance.freeEntities.front();
                instance.freeEntities.pop_front();
            }

            instance.metadata.writeComponents[entity.index][0] = true;
            instance.metadata.writeComponents[entity.index].generation = entity.generation;
            auto &validEntities = instance.metadata.writeValidEntities;
            instance.metadata.validEntityIndexes[entity.index] = validEntities.size();
            validEntities.emplace_back(entity);

            return entity;
        }

        template<typename... Tn>
        inline bool Has() const {
            static_assert(all_global_components<Tn...>(), "Only global components can be accessed without an Entity");

            if (permissions[0]) {
                return instance.template BitsetHas<Tn...>(instance.globalWriteMetadata);
            } else {
                return instance.template BitsetHas<Tn...>(instance.globalReadMetadata);
            }
        }

        template<typename... Tn>
        inline bool Had() const {
            static_assert(all_global_components<Tn...>(), "Only global components can be accessed without an Entity");

            return instance.template BitsetHas<Tn...>(instance.globalReadMetadata);
        }

        template<typename T, typename ReturnType =
                                 std::conditional_t<is_write_allowed<std::remove_cv_t<T>, LockType>::value, T, const T>>
        inline ReturnType &Get() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(is_write_allowed<CompType, LockType>() || std::is_const<ReturnType>(),
                "Can't get non-const reference of read only Component.");
            static_assert(is_global_component<CompType>(), "Only global components can be accessed without an Entity");

            if (!std::is_const<ReturnType>()) base->template SetAccessFlag<CompType>(true);

            auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<CompType>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<CompType>()] = true;
                    instance.template Storage<CompType>().writeComponents.resize(1);
                    // Reset value before allowing reading.
                    instance.template Storage<CompType>().writeComponents[0] = {};
                } else {
                    throw std::runtime_error(
                        "Missing global component of type: " + std::string(typeid(CompType).name()));
                }
            }
            if (instance.template BitsetHas<CompType>(permissions)) {
                return instance.template Storage<CompType>().writeComponents[0];
            } else {
                return instance.template Storage<CompType>().readComponents[0];
            }
        }

        template<typename T>
        inline const T &GetPrevious() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(is_global_component<CompType>(), "Only global components can be accessed without an Entity");

            if (!instance.template BitsetHas<CompType>(instance.globalReadMetadata)) {
                throw std::runtime_error("Missing global component of type: " + std::string(typeid(CompType).name()));
            }
            return instance.template Storage<CompType>().readComponents[0];
        }

        template<typename T>
        inline T &Set(T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->template SetAccessFlag<T>(true);

            auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<T>()] = true;
                    instance.template Storage<T>().writeComponents.resize(1);
                } else {
                    throw std::runtime_error("Missing global component of type: " + std::string(typeid(T).name()));
                }
            }
            return instance.template Storage<T>().writeComponents[0] = value;
        }

        template<typename T, typename... Args>
        inline T &Set(Args &&...args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->template SetAccessFlag<T>(true);

            auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<T>()] = true;
                    instance.template Storage<T>().writeComponents.resize(1);
                } else {
                    throw std::runtime_error("Missing global component of type: " + std::string(typeid(T).name()));
                }
            }
            return instance.template Storage<T>().writeComponents[0] = T(std::forward<Args>(args)...);
        }

        template<typename... Tn>
        inline void Unset() const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");
            static_assert(all_global_components<Tn...>(), "Only global components can be unset without an Entity");

            (RemoveComponents<Tn>(), ...);
        }

        template<typename Event>
        inline Observer<ECS, Event> Watch() const {
            static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to watch for ecs changes.");

            auto &observerList = instance.template Observers<Event>();
            auto &eventList = observerList.observers.emplace_back(std::make_shared<std::deque<Event>>());
            return Observer(instance, eventList);
        }

        template<typename Event>
        inline void StopWatching(Observer<ECS, Event> &observer) const {
            static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to stop an observer.");
            auto eventList = observer.eventListWeak.lock();
            auto &observers = instance.template Observers<Event>().observers;
            observers.erase(std::remove(observers.begin(), observers.end(), eventList), observers.end());
            observer.eventListWeak.reset();
        }

        template<typename... PermissionsSubset>
        inline Lock<ECS, PermissionsSubset...> Subset() const {
            using NewLockType = Lock<ECS, PermissionsSubset...>;
            static_assert(is_add_remove_allowed<LockType>() || !is_add_remove_allowed<NewLockType>(),
                "AddRemove permission is missing.");
            static_assert(std::conjunction<is_lock_subset<AllComponentTypes, NewLockType, LockType>...>(),
                "Lock types are not a subset of existing permissions.");

            return Lock<ECS, PermissionsSubset...>(*this);
        }

        long UseCount() const {
            return base.use_count();
        }

    private:
        template<typename T>
        inline void AllocateComponents(size_t count) const {
            if constexpr (!is_global_component<T>()) {
                base->template SetAccessFlag<T>(true);

                size_t newSize = instance.template Storage<T>().writeComponents.size() + count;
                instance.template Storage<T>().writeComponents.resize(newSize);
                instance.template Storage<T>().validEntityIndexes.resize(newSize);
            } else {
                (void)count; // Unreferenced parameter warning on MSVC
            }
        }

        template<typename T>
        inline void RemoveComponents(size_t index) const {
            if constexpr (!is_global_component<T>()) { // Ignore global components
                auto &metadata = instance.metadata.writeComponents[index];
                if (instance.template BitsetHas<T>(metadata)) {
                    base->writeAccessedFlags[0] = true;
                    base->template SetAccessFlag<T>(true);

                    metadata[1 + instance.template GetComponentIndex<T>()] = false;
                    auto &compIndex = instance.template Storage<T>();
                    compIndex.writeComponents[index] = {};
                    size_t validIndex = compIndex.validEntityIndexes[index];
                    compIndex.writeValidEntities[validIndex] = Entity();
                }
            }
        }

        template<typename T>
        inline void RemoveComponents() const {
            static_assert(is_global_component<T>(), "Only global components can be removed without an Entity");

            auto &metadata = instance.globalWriteMetadata;
            if (instance.template BitsetHas<T>(metadata)) {
                base->writeAccessedFlags[0] = true;
                base->template SetAccessFlag<T>(true);

                metadata[1 + instance.template GetComponentIndex<T>()] = false;
                instance.template Storage<T>().writeComponents[0] = {};
            }
        }

        inline void RemoveAllComponents(size_t index) const {
            (RemoveComponents<AllComponentTypes>(index), ...);
        }

        template<typename, typename...>
        friend class Lock;
        template<typename, typename...>
        friend class DynamicLock;
        friend struct Entity;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... StaticPermissions>
    class DynamicLock<ECSType<AllComponentTypes...>, StaticPermissions...>
        : public Lock<ECSType<AllComponentTypes...>, StaticPermissions...> {
    private:
        using ECS = ECSType<AllComponentTypes...>;

        std::bitset<1 + sizeof...(AllComponentTypes)> readPermissions;

    public:
        template<typename LockType>
        DynamicLock(const LockType &lock) : Lock<ECS, StaticPermissions...>(lock) {
            readPermissions[0] = true;
            ((readPermissions[1 + this->instance.template GetComponentIndex<AllComponentTypes>()] =
                     is_read_allowed<AllComponentTypes, LockType>()),
                ...);
        }

        template<typename... DynamicPermissions>
        std::optional<Lock<ECS, DynamicPermissions...>> TryLock() const {
            if constexpr (Lock<ECS, DynamicPermissions...>::template has_all_permissions<StaticPermissions...>()) {
                return Lock<ECS, DynamicPermissions...>(this->instance, this->base, this->permissions);
            } else {
                std::bitset<1 + sizeof...(AllComponentTypes)> requestedRead, requestedWrite;
                requestedRead[0] = true;
                requestedWrite[1] = Tecs::is_add_remove_allowed<DynamicPermissions...>();
                ((requestedRead[1 + this->instance.template GetComponentIndex<AllComponentTypes>()] =
                         is_read_allowed<AllComponentTypes, DynamicPermissions...>()),
                    ...);
                ((requestedWrite[1 + this->instance.template GetComponentIndex<AllComponentTypes>()] =
                         is_write_allowed<AllComponentTypes, DynamicPermissions...>()),
                    ...);
                if ((requestedRead & readPermissions) == requestedRead &&
                    (requestedWrite & this->permissions) == requestedWrite) {
                    return Lock<ECS, DynamicPermissions...>(this->instance, this->base, this->permissions);
                }
                return {};
            }
        }
    };
}; // namespace Tecs
