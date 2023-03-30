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
    template<typename, typename...>
    class EntityLock;

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
        std::shared_ptr<Transaction<ECS>> base;
        std::bitset<1 + sizeof...(AllComponentTypes)> writePermissions;

    public:
        // Start a new transaction
        inline Lock(ECS &instance) : instance(instance) {
            base = std::make_shared<Transaction<ECS>>(*this);
            base->readLockReferences[0]++;
            if constexpr (is_add_remove_allowed<LockType>()) base->writeLockReferences[0]++;
            (base->template AcquireLockReference<LockType, AllComponentTypes>(), ...);

            writePermissions = base->writePermissions;
        }

        // Returns true if this lock type can be constructed from a lock with the specified source permissions
        template<typename... SourcePermissions>
        static constexpr bool is_lock_subset() {
            using SourceLockType = Lock<ECS, SourcePermissions...>;
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
        inline Lock(const LockType &source)
            : instance(source.instance), base(source.base), writePermissions(source.writePermissions) {
            base->readLockReferences[0]++;
            if constexpr (is_add_remove_allowed<LockType>()) base->writeLockReferences[0]++;
            (base->template AcquireLockReference<LockType, AllComponentTypes>(), ...);
        }

        // Reference a subset of an existing transaction
        template<typename... SourcePermissions, std::enable_if_t<is_lock_subset<SourcePermissions...>(), int> = 0>
        inline Lock(const Lock<ECS, SourcePermissions...> &source)
            : instance(source.instance), base(source.base), writePermissions(source.writePermissions) {
            base->readLockReferences[0]++;
            if constexpr (is_add_remove_allowed<LockType>()) base->writeLockReferences[0]++;
            (base->template AcquireLockReference<LockType, AllComponentTypes>(), ...);
        }

        inline ~Lock() {
            base->readLockReferences[0]--;
            if constexpr (is_add_remove_allowed<LockType>()) base->writeLockReferences[0]--;
            (base->template ReleaseLockReference<LockType, AllComponentTypes>(), ...);
        }

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

            if (writePermissions[0]) {
                return instance.template Storage<T>().writeValidEntities;
            } else {
                return instance.template Storage<T>().readValidEntities;
            }
        }

        inline const EntityView PreviousEntities() const {
            return instance.metadata.readValidEntities;
        }

        inline const EntityView Entities() const {
            if (writePermissions[0]) {
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

            if (writePermissions[0]) {
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

            auto &metadata = writePermissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<CompType>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<CompType>()] = true;
                    instance.template Storage<CompType>().writeComponents.resize(1);
                    // Reset value before allowing reading.
                    instance.template Storage<CompType>().writeComponents[0] = {};
                } else {
                    throw std::runtime_error("Missing global component of type: "s + typeid(CompType).name());
                }
            }
            if (instance.template BitsetHas<CompType>(writePermissions)) {
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
                throw std::runtime_error("Missing global component of type: "s + typeid(CompType).name());
            }
            return instance.template Storage<CompType>().readComponents[0];
        }

        template<typename T>
        inline T &Set(T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->template SetAccessFlag<T>(true);

            auto &metadata = writePermissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<T>()] = true;
                    instance.template Storage<T>().writeComponents.resize(1);
                } else {
                    throw std::runtime_error("Missing global component of type: "s + typeid(T).name());
                }
            }
            return instance.template Storage<T>().writeComponents[0] = value;
        }

        template<typename T, typename... Args>
        inline T &Set(Args &&...args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->template SetAccessFlag<T>(true);

            auto &metadata = writePermissions[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    metadata[1 + instance.template GetComponentIndex<T>()] = true;
                    instance.template Storage<T>().writeComponents.resize(1);
                } else {
                    throw std::runtime_error("Missing global component of type: "s + typeid(T).name());
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
            static_assert(has_permissions<PermissionsSubset...>(),
                "Lock types are not a subset of existing permissions.");

            return Lock<ECS, PermissionsSubset...>(*this);
        }

        long UseCount() const {
            return base.use_count();
        }

    private:
        // Private constructor for DynamicLock to Lock conversion
        template<typename... SourcePermissions>
        inline Lock(ECS &instance, decltype(base) base, decltype(writePermissions) writePermissions)
            : instance(instance), base(base), writePermissions(writePermissions) {
            base->readLockReferences[0]++;
            if constexpr (is_add_remove_allowed<LockType>()) base->writeLockReferences[0]++;
            (base->template AcquireLockReference<LockType, AllComponentTypes>(), ...);
        }

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

        template<typename>
        friend class Transaction;
        template<typename, typename...>
        friend class Lock;
        template<typename, typename...>
        friend class DynamicLock;
        template<typename, typename...>
        friend class EntityLock;
        friend struct Entity;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... StaticPermissions>
    class DynamicLock<ECSType<AllComponentTypes...>, StaticPermissions...>
        : public Lock<ECSType<AllComponentTypes...>, StaticPermissions...> {
    private:
        using ECS = ECSType<AllComponentTypes...>;

        template<typename... Permissions>
        static const auto generateReadBitset() {
            std::bitset<1 + sizeof...(AllComponentTypes)> result;
            result[0] = true;
            ((result[1 + ECS::template GetComponentIndex<AllComponentTypes>()] =
                     is_read_allowed<AllComponentTypes, Permissions...>()),
                ...);
            return result;
        }

        template<typename... Permissions>
        static const auto generateWriteBitset() {
            std::bitset<1 + sizeof...(AllComponentTypes)> result;
            result[0] = Tecs::is_add_remove_allowed<Permissions...>();
            ((result[1 + ECS::template GetComponentIndex<AllComponentTypes>()] =
                     is_write_allowed<AllComponentTypes, Permissions...>()),
                ...);
            return result;
        }

    public:
        template<typename LockType>
        inline DynamicLock(const LockType &source) : Lock<ECS, StaticPermissions...>(source) {}

        template<typename... DynamicPermissions>
        std::optional<Lock<ECS, DynamicPermissions...>> TryLock() const {
            if constexpr (Lock<ECS, StaticPermissions...>::template has_permissions<DynamicPermissions...>()) {
                return Lock<ECS, DynamicPermissions...>(this->instance, this->base, this->writePermissions);
            } else {
                static const auto requestedRead = generateReadBitset<DynamicPermissions...>();
                static const auto requestedWrite = generateWriteBitset<DynamicPermissions...>();
                if ((requestedRead & this->base->readPermissions) == requestedRead &&
                    (requestedWrite & this->base->writePermissions) == requestedWrite) {
                    return Lock<ECS, DynamicPermissions...>(this->instance, this->base, this->writePermissions);
                }
                return {};
            }
        }

        template<typename, typename...>
        friend class DynamicLock;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... Permissions>
    class EntityLock<ECSType<AllComponentTypes...>, Permissions...> {
    private:
        using ECS = ECSType<AllComponentTypes...>;
        using LockType = Lock<ECS, Permissions...>;

        const LockType lock;

    public:
        const Entity entity;

        template<typename SourceLockType>
        inline EntityLock(const SourceLockType &source, const Entity &entity) : lock(source), entity(entity) {
            static_assert(!is_add_remove_allowed<Permissions...>(),
                "EntityLock with AddRemove permissions is not supported");
        }

        template<typename... SourcePermissions>
        inline EntityLock(const EntityLock<ECS, SourcePermissions...> &source)
            : lock(source.lock), entity(source.entity) {}

        inline constexpr ECS &GetInstance() const {
            return lock.instance;
        }

#ifndef TECS_HEADER_ONLY
        inline size_t GetTransactionId() const {
            return lock.base->transactionId;
        }
#endif

        template<typename T>
        inline const EntityView PreviousEntitiesWith() const {
            static_assert(!is_global_component<T>(), "Entities can't have global components");
            return lock.template PreviousEntitiesWith<T>();
        }

        template<typename T>
        inline const EntityView EntitiesWith() const {
            static_assert(!is_global_component<T>(), "Entities can't have global components");
            return lock.template EntitiesWith<T>();
        }

        inline const EntityView PreviousEntities() const {
            return lock.template PreviousEntities();
        }

        inline const EntityView Entities() const {
            return lock.template Entities();
        }

        inline bool Exists() const {
            return entity.Exists(lock);
        }

        inline bool Existed() const {
            return entity.Existed(lock);
        }

        template<typename... Tn>
        inline bool Has() const {
            if constexpr (all_global_components<Tn...>()) {
                return lock.template Had<Tn...>();
            } else {
                return entity.Has<Tn...>(lock);
            }
        }

        template<typename... Tn>
        inline bool Had() const {
            if constexpr (all_global_components<Tn...>()) {
                return lock.template Had<Tn...>();
            } else {
                return entity.Had<Tn...>(lock);
            }
        }

        template<typename T>
        inline auto &Get() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            if constexpr (is_global_component<CompType>()) {
                return lock.template Get<T>();
            } else {
                return entity.Get<T>(lock);
            }
        }

        template<typename T>
        inline const T &GetPrevious() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            if constexpr (is_global_component<CompType>()) {
                return lock.template GetPrevious<T>();
            } else {
                return entity.GetPrevious<T>(lock);
            }
        }

        template<typename T>
        inline T &Set(T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            if constexpr (is_global_component<T>()) {
                return lock.template Set<T>(value);
            } else {
                return entity.Set<T>(value, lock);
            }
        }

        template<typename T, typename... Args>
        inline T &Set(Args &&...args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            if constexpr (is_global_component<T>()) {
                return lock.template Set<T>(std::forward<Args>(args)...);
            } else {
                return entity.Set<T>(std::forward<Args>(args)..., lock);
            }
        }

        template<typename... PermissionsSubset>
        inline EntityLock<ECS, PermissionsSubset...> Subset() const {
            static_assert(LockType::template has_permissions<PermissionsSubset...>(),
                "Lock types are not a subset of existing permissions.");

            return EntityLock<ECS, PermissionsSubset...>(*this);
        }

        long UseCount() const {
            return lock.base.use_count();
        }

        template<typename, typename...>
        friend class EntityLock;
        friend struct Entity;
    };
} // namespace Tecs
