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
        inline Lock(const Lock<ECS, PermissionsSource...> &source)
            : instance(source.instance), base(source.base), permissions(source.permissions) {}

        inline constexpr ECS &GetInstance() const {
            return instance;
        }

#ifndef TECS_HEADER_ONLY
        inline size_t GetTransactionId() const {
            return base->transactionId;
        }
#endif

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
                    TECS_ENTITY_INDEX_TYPE newIndex = (TECS_ENTITY_INDEX_TYPE)(nextIndex + count);
                    instance.metadata.AccessEntity(newIndex);
                    instance.freeEntities.emplace_back(newIndex, 1, (TECS_ENTITY_ECS_IDENTIFIER_TYPE)instance.ecsId);
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
            instance.metadata.AccessEntity(entity.index);

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

            if (!std::is_const<ReturnType>()) base->template SetAccessFlag<CompType>();

#ifndef TECS_UNCHECKED_MODE
            auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
#endif

            auto &storage = instance.template Storage<CompType>();
            if constexpr (is_add_remove_allowed<LockType>()) {
#ifdef TECS_UNCHECKED_MODE
                auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
#endif
                if (!instance.template BitsetHas<CompType>(metadata)) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<CompType>()] = true;
                    storage.writeComponents.resize(1);
                    // Reset value before allowing reading.
                    storage.writeComponents[0] = {};
                }
#ifndef TECS_UNCHECKED_MODE
            } else if (!instance.template BitsetHas<CompType>(metadata)) {
                throw std::runtime_error("Missing global component of type: " + std::string(typeid(CompType).name()));
#endif
            }
            if (instance.template BitsetHas<CompType>(permissions)) {
                return storage.writeComponents[0];
            } else {
                return storage.readComponents[0];
            }
        }

        template<typename T>
        inline const T &GetPrevious() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(is_global_component<CompType>(), "Only global components can be accessed without an Entity");

#ifndef TECS_UNCHECKED_MODE
            if (!instance.template BitsetHas<CompType>(instance.globalReadMetadata)) {
                throw std::runtime_error("Missing global component of type: " + std::string(typeid(CompType).name()));
            }
#endif
            return instance.template Storage<CompType>().readComponents[0];
        }

        template<typename T>
        inline T &Set(T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->template SetAccessFlag<T>();

#ifndef TECS_UNCHECKED_MODE
            auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
#endif

            if constexpr (is_add_remove_allowed<LockType>()) {
#ifdef TECS_UNCHECKED_MODE
                auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
#endif
                if (!instance.template BitsetHas<T>(metadata)) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<T>()] = true;
                    instance.template Storage<T>().writeComponents.resize(1);
                }
#ifndef TECS_UNCHECKED_MODE
            } else if (!instance.template BitsetHas<T>(metadata)) {
                throw std::runtime_error("Missing global component of type: " + std::string(typeid(T).name()));
#endif
            }
            return instance.template Storage<T>().writeComponents[0] = value;
        }

        template<typename T, typename... Args>
        inline T &Set(Args &&...args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->template SetAccessFlag<T>();

#ifndef TECS_UNCHECKED_MODE
            auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
#endif

            if constexpr (is_add_remove_allowed<LockType>()) {
#ifdef TECS_UNCHECKED_MODE
                auto &metadata = permissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
#endif
                if (!instance.template BitsetHas<T>(metadata)) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<T>()] = true;
                    instance.template Storage<T>().writeComponents.resize(1);
                }
#ifndef TECS_UNCHECKED_MODE
            } else if (!instance.template BitsetHas<T>(metadata)) {
                throw std::runtime_error("Missing global component of type: " + std::string(typeid(T).name()));
#endif
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

            auto eventList = std::make_shared<std::deque<Event>>();
            if constexpr (std::is_same<Event, EntityAddRemoveEvent>()) {
                instance.entityAddEvents.observers.emplace_back(eventList);
                instance.entityRemoveEvents.observers.emplace_back(eventList);
            } else if constexpr (Event::isAddRemove()) {
                auto &storage = instance.template Storage<typename Event::ComponentType>();
                storage.componentAddEvents.observers.emplace_back(eventList);
                storage.componentRemoveEvents.observers.emplace_back(eventList);
            } else {
                auto &storage = instance.template Storage<typename Event::ComponentType>();
                storage.componentModifyEvents.observers.emplace_back(eventList);
            }
            return Observer(instance, eventList);
        }

        template<typename Event>
        inline void StopWatching(Observer<ECS, Event> &observer) const {
            static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to stop an observer.");
            auto eventList = observer.eventListWeak.lock();
            if constexpr (std::is_same<Event, EntityAddRemoveEvent>()) {
                auto &observers = instance.entityAddEvents.observers;
                observers.erase(std::remove(observers.begin(), observers.end(), eventList), observers.end());
                auto &observers2 = instance.entityRemoveEvents.observers;
                observers2.erase(std::remove(observers2.begin(), observers2.end(), eventList), observers2.end());
            } else if constexpr (Event::isAddRemove()) {
                auto &storage = instance.template Storage<typename Event::ComponentType>();
                auto &observers = storage.componentAddEvents.observers;
                observers.erase(std::remove(observers.begin(), observers.end(), eventList), observers.end());
                auto &observers2 = storage.componentRemoveEvents.observers;
                observers2.erase(std::remove(observers2.begin(), observers2.end(), eventList), observers2.end());
            } else {
                auto &storage = instance.template Storage<typename Event::ComponentType>();
                auto &observers = storage.componentModifyEvents.observers;
                observers.erase(std::remove(observers.begin(), observers.end(), eventList), observers.end());
            }
            observer.eventListWeak.reset();
        }

        template<typename... PermissionsSubset>
        inline Lock<ECS, PermissionsSubset...> Subset() const {
            using NewLockType = Lock<ECS, PermissionsSubset...>;
            static_assert(has_permissions<NewLockType>(), "Lock types are not a subset of existing permissions.");

            return NewLockType(*this);
        }

        /**
         * Convert this lock into a read-only variant.
         *
         * Reads performed through this lock will not be able to see writes from the parent lock, and instead will
         * return the previous value.
         */
        inline auto ReadOnlySubset() const {
            using NewLockType = Lock<ECS, typename FlattenPermissions<LockType, AllComponentTypes...>::type_readonly>;
            static_assert(has_permissions<NewLockType>(), "Lock types are not a subset of existing permissions.");
            return NewLockType(this->instance, this->base, {});
        }

        long UseCount() const {
            return base.use_count();
        }

    private:
        template<typename T>
        inline void AllocateComponents(size_t count) const {
            if constexpr (!is_global_component<T>()) {
                base->template SetAccessFlag<T>();

                auto &storage = instance.template Storage<T>();
                size_t newSize = storage.writeComponents.size() + count;
                storage.writeComponents.resize(newSize);
                storage.validEntityIndexes.resize(newSize);
            } else {
                (void)count; // Unreferenced parameter warning on MSVC
            }
        }

        template<typename T>
        inline void RemoveComponents(TECS_ENTITY_INDEX_TYPE index) const {
            if constexpr (!is_global_component<T>()) { // Ignore global components
                auto &metadata = instance.metadata.writeComponents[index];
                if (instance.template BitsetHas<T>(metadata)) {
                    base->writeAccessedFlags[0] = true;
                    base->template SetAccessFlag<T>(index);

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
                base->template SetAccessFlag<T>();

                metadata[1 + instance.template GetComponentIndex<T>()] = false;
                instance.template Storage<T>().writeComponents[0] = {};
            }
        }

        inline void RemoveAllComponents(TECS_ENTITY_INDEX_TYPE index) const {
            (RemoveComponents<AllComponentTypes>(index), ...);
        }

        template<typename, typename...>
        friend class Lock;
        template<typename, typename...>
        friend class DynamicLock;
        friend struct Entity;
    };

    template<typename>
    struct is_dynamic_lock : std::false_type {};

    template<typename ECS, typename... StaticPermissions>
    struct is_dynamic_lock<DynamicLock<ECS, StaticPermissions...>> : std::true_type {};

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... StaticPermissions>
    class DynamicLock<ECSType<AllComponentTypes...>, StaticPermissions...>
        : public Lock<ECSType<AllComponentTypes...>, StaticPermissions...> {
    private:
        using ECS = ECSType<AllComponentTypes...>;
        using BaseLockType = Lock<ECS, StaticPermissions...>;

        const std::bitset<1 + sizeof...(AllComponentTypes)> readPermissions;

        template<typename LockType>
        static inline constexpr auto generateReadBitset() {
            std::bitset<1 + sizeof...(AllComponentTypes)> result;
            if constexpr (sizeof...(AllComponentTypes) < 64) {
                // clang-format off
                constexpr uint64_t mask = 1 | ((
                    ((uint64_t)is_read_allowed<AllComponentTypes, LockType>())
                        << (1 + ECS::template GetComponentIndex<AllComponentTypes>())
                ) | ...);
                // clang-format on
                result = std::bitset<1 + sizeof...(AllComponentTypes)>(mask);
            } else {
                result[0] = true;
                ((result[1 + ECS::template GetComponentIndex<AllComponentTypes>()] =
                         is_read_allowed<AllComponentTypes, LockType>()),
                    ...);
            }
            return result;
        }

        template<typename LockType>
        static inline constexpr auto generateReadBitset(const LockType &lock) {
            if constexpr (is_dynamic_lock<LockType>()) {
                return lock.readPermissions;
            } else {
                return generateReadBitset<LockType>();
            }
        }

        template<typename LockType>
        static inline constexpr auto generateWriteBitset() {
            std::bitset<1 + sizeof...(AllComponentTypes)> result;
            if constexpr (sizeof...(AllComponentTypes) < 64) {
                // clang-format off
                constexpr uint64_t mask = (uint64_t)Tecs::is_add_remove_allowed<LockType>() | ((
                    ((uint64_t)is_write_allowed<AllComponentTypes, LockType>())
                        << (1 + ECS::template GetComponentIndex<AllComponentTypes>())
                ) | ...);
                // clang-format on
                result = std::bitset<1 + sizeof...(AllComponentTypes)>(mask);
            } else {
                result[0] = Tecs::is_add_remove_allowed<LockType>();
                ((result[1 + ECS::template GetComponentIndex<AllComponentTypes>()] =
                         is_write_allowed<AllComponentTypes, LockType>()),
                    ...);
            }
            return result;
        }

    public:
        // Create from an existing static lock
        template<typename LockType>
        DynamicLock(const LockType &lock) : BaseLockType(lock), readPermissions(generateReadBitset(lock)) {}

        template<typename... DynamicPermissions>
        std::optional<Lock<ECS, DynamicPermissions...>> TryLock() const {
            using DynamicLockType = Lock<ECS, DynamicPermissions...>;
            if constexpr (BaseLockType::template has_permissions<DynamicLockType>()) {
                return DynamicLockType(this->instance, this->base, this->permissions);
            } else {
                static constexpr auto requestedRead = generateReadBitset<DynamicLockType>();
                static constexpr auto requestedWrite = generateWriteBitset<DynamicLockType>();
                if ((requestedRead & readPermissions) == requestedRead &&
                    (requestedWrite & this->permissions) == requestedWrite) {
                    return DynamicLockType(this->instance, this->base, this->permissions);
                }
                return {};
            }
        }

    private:
        template<typename, typename...>
        friend class DynamicLock;
    };
}; // namespace Tecs
