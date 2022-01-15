#pragma once

#include "Tecs_entity.hh"
#include "Tecs_observer.hh"
#include "Tecs_permissions.hh"
#include "Tecs_transaction.hh"
#include "nonstd/span.hpp"

#include <bitset>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#ifndef TECS_ENTITY_ALLOCATION_BATCH_SIZE
    #define TECS_ENTITY_ALLOCATION_BATCH_SIZE 1000
#endif

static_assert(TECS_ENTITY_ALLOCATION_BATCH_SIZE > 0, "At least 1 entity needs to be allocated at once.");

namespace Tecs {
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
        typename ECS::ComponentBitset permissions;

        inline const auto &ReadMetadata(const TECS_ENTITY_ID_TYPE &id) const {
            if (id >= instance.metadata.readComponents.size()) return instance.EmptyMetadataRef();
            return instance.metadata.readComponents[id];
        }

        inline const auto &WriteMetadata(const TECS_ENTITY_ID_TYPE &id) const {
            if (id >= instance.metadata.writeComponents.size()) return instance.EmptyMetadataRef();
            return instance.metadata.writeComponents[id];
        }

    public:
        // Start a new transaction
        inline Lock(ECS &instance) : instance(instance), base(new Transaction<ECS, Permissions...>(instance)) {
            permissions[0] = is_add_remove_allowed<LockType>();
            // clang-format off
            ((
                permissions[1 + instance.template GetComponentIndex<AllComponentTypes>()] = is_write_allowed<AllComponentTypes, LockType>()
            ), ...);
            // clang-format on
        }

        // Reference an existing transaction
        template<typename... PermissionsSource>
        inline Lock(const Lock<ECS, PermissionsSource...> &source)
            : instance(source.instance), base(source.base), permissions(source.permissions) {
            using SourceLockType = Lock<ECS, PermissionsSource...>;
            static_assert(is_add_remove_allowed<SourceLockType>() || !is_add_remove_allowed<LockType>(),
                "AddRemove permission is missing.");
            static_assert(std::conjunction<is_lock_subset<AllComponentTypes, LockType, SourceLockType>...>(),
                "Lock types are not a subset of existing permissions.");
        }

        inline constexpr ECS<AllComponentTypes...> &GetInstance() const {
            return instance;
        }

        template<typename T>
        inline constexpr const nonstd::span<Entity> PreviousEntitiesWith() const {
            static_assert(!is_global_component<T>(), "Entities can't have global components");

            return instance.template Storage<T>().readValidEntities;
        }

        template<typename T>
        inline constexpr const nonstd::span<Entity> EntitiesWith() const {
            static_assert(!is_global_component<T>(), "Entities can't have global components");

            if (permissions[0]) {
                return instance.template Storage<T>().writeValidEntities;
            } else {
                return instance.template Storage<T>().readValidEntities;
            }
        }

        inline constexpr const nonstd::span<Entity> PreviousEntities() const {
            return instance.metadata.readValidEntities;
        }

        inline constexpr const nonstd::span<Entity> Entities() const {
            if (permissions[0]) {
                return instance.metadata.writeValidEntities;
            } else {
                return instance.metadata.readValidEntities;
            }
        }

        inline Entity NewEntity() const {
            static_assert(is_add_remove_allowed<LockType>(), "Lock does not have AddRemove permission.");
            base->writeAccessedFlags[0] = true;

            Entity entity;
            if (instance.freeEntities.empty()) {
                // Allocate a new set of entities and components
                (AllocateComponents<AllComponentTypes>(TECS_ENTITY_ALLOCATION_BATCH_SIZE), ...);
                size_t nextIndex = instance.metadata.writeComponents.size();
                size_t newSize = nextIndex + TECS_ENTITY_ALLOCATION_BATCH_SIZE;
                instance.metadata.writeComponents.resize(newSize);
                instance.metadata.validEntityIndexes.resize(newSize);

                // Add all but 1 of the new Entity ids to the free list.
                for (size_t count = 1; count < TECS_ENTITY_ALLOCATION_BATCH_SIZE; count++) {
                    instance.freeEntities.emplace_back(nextIndex + count);
                }
                entity.id = nextIndex;
            } else {
                entity = instance.freeEntities.front();
                instance.freeEntities.pop_front();
            }

            instance.metadata.writeComponents[entity.id][0] = true;
            auto &validEntities = instance.metadata.writeValidEntities;
            instance.metadata.validEntityIndexes[entity.id] = validEntities.size();
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

            if (!std::is_const<ReturnType>()) base->SetAccessFlag<CompType>(true);

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
            base->SetAccessFlag<T>(true);

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
            base->SetAccessFlag<T>(true);

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

    private:
        template<typename T>
        inline void AllocateComponents(size_t count) const {
            if constexpr (!is_global_component<T>()) {
                base->SetAccessFlag<T>(true);

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
                    base->SetAccessFlag<T>(true);

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
                base->SetAccessFlag<T>(true);

                metadata[1 + instance.template GetComponentIndex<T>()] = false;
                instance.template Storage<T>().writeComponents[0] = {};
            }
        }

        inline void RemoveAllComponents(size_t index) const {
            (RemoveComponents<AllComponentTypes>(index), ...);
        }

        template<typename, typename...>
        friend class Lock;
        friend struct Entity;
    };
}; // namespace Tecs
