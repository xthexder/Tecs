#pragma once

#include "Tecs_entity.hh"
#include "Tecs_locks.hh"
#include "Tecs_observer.hh"
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #include "Tecs_tracing.hh"
#endif
#include "nonstd/span.hpp"

#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#ifndef TECS_ENTITY_ALLOCATION_BATCH_SIZE
    #define TECS_ENTITY_ALLOCATION_BATCH_SIZE 1000
#endif

static_assert(TECS_ENTITY_ALLOCATION_BATCH_SIZE > 0, "At least 1 entity needs to be allocated at once.");

namespace Tecs {
#ifndef TECS_HEADER_ONLY
    // Used for detecting nested transactions
    extern thread_local std::vector<size_t> activeTransactions;
    extern std::atomic_size_t nextEcsId;
#endif

    /**
     * When a Transaction is started, the relevant parts of the ECS are locked based on the Transactions Permissons.
     * The permissions can then be referenced by passing around Lock objects.
     *
     * Upon deconstruction, a Transaction will commit any changes written during its lifespan to the instance.
     * Once a Transaction is deconstructed, all Locks referencing its permissions become invalid.
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes>
    class BaseTransaction {
    public:
        BaseTransaction(ECSType<AllComponentTypes...> &instance) : instance(instance) {
#ifndef TECS_HEADER_ONLY
            for (auto &id : activeTransactions) {
                if (id == instance.ecsId) throw std::runtime_error("Nested transactions are not allowed");
            }
            activeTransactions.push_back(instance.ecsId);
#endif
        }
        // Delete copy constructor
        BaseTransaction(const BaseTransaction &) = delete;

        virtual ~BaseTransaction() {
#ifndef TECS_HEADER_ONLY
            activeTransactions.erase(std::remove(activeTransactions.begin(), activeTransactions.end(), instance.ecsId));
#endif
        }

    protected:
        ECSType<AllComponentTypes...> &instance;

        std::bitset<1 + sizeof...(AllComponentTypes)> writeAccessedFlags;

        template<typename, typename...>
        friend class Lock;
        friend struct Entity;
    };

    template<typename... AllComponentTypes, typename... Permissions>
    class Transaction<ECS<AllComponentTypes...>, Permissions...> : public BaseTransaction<ECS, AllComponentTypes...> {
    private:
        using LockType = Lock<ECS<AllComponentTypes...>, Permissions...>;

    public:
        inline Transaction(ECS<AllComponentTypes...> &instance) : BaseTransaction<ECS, AllComponentTypes...>(instance) {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            instance.transactionTrace.Trace(TraceEvent::Type::TransactionStart);
#endif

            std::bitset<1 + sizeof...(AllComponentTypes)> acquired;
            // Templated lambda functions for Lock/Unlock so they can be looped over at runtime.
            std::array<std::function<bool(bool)>, acquired.size()> lockFuncs = {
                [&instance](bool block) {
                    if (is_add_remove_allowed<LockType>()) {
                        return instance.validIndex.WriteLock(block);
                    } else {
                        return instance.validIndex.ReadLock(block);
                    }
                },
                [&instance](bool block) {
                    if (is_write_allowed<AllComponentTypes, LockType>()) {
                        return instance.template Storage<AllComponentTypes>().WriteLock(block);
                    } else if (is_read_allowed<AllComponentTypes, LockType>()) {
                        return instance.template Storage<AllComponentTypes>().ReadLock(block);
                    }
                    // This component type isn't part of the lock, skip.
                    return true;
                }...};
            std::array<std::function<void()>, acquired.size()> unlockFuncs = {
                [&instance]() {
                    if (is_add_remove_allowed<LockType>()) {
                        return instance.validIndex.WriteUnlock();
                    } else {
                        return instance.validIndex.ReadUnlock();
                    }
                },
                [&instance]() {
                    if (is_write_allowed<AllComponentTypes, LockType>()) {
                        instance.template Storage<AllComponentTypes>().WriteUnlock();
                    } else if (is_read_allowed<AllComponentTypes, LockType>()) {
                        instance.template Storage<AllComponentTypes>().ReadUnlock();
                    }
                    // This component type isn't part of the lock, skip.
                }...};

            // Attempt to lock all applicable components and rollback if not all locks can be immediately acquired.
            // This should only block while no locks are held to prevent deadlocks.
            bool rollback = false;
            for (size_t i = 0; !acquired.all(); i = (i + 1) % acquired.size()) {
                if (rollback) {
                    if (acquired[i]) {
                        unlockFuncs[i]();
                        acquired[i] = false;
                        continue;
                    } else if (acquired.none()) {
                        rollback = false;
                    }
                }
                if (!rollback) {
                    if (lockFuncs[i](acquired.none())) {
                        acquired[i] = true;
                    } else {
                        rollback = true;
                    }
                }
            }

            if (is_add_remove_allowed<LockType>()) {
                auto &observerList = this->instance.template Observers<EntityEvent>();
                if (!observerList.writeQueue) { observerList.writeQueue = std::make_shared<std::deque<EntityEvent>>(); }
                InitObserverEventQueues<AllComponentTypes...>();
            }
        }

        inline ~Transaction() {
            if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                // Rebuild writeValidEntities, validEntityIndexes, and freeEntities with the new entity set.
                this->instance.validIndex.writeValidEntities.clear();
                ClearValidEntities<AllComponentTypes...>();
                this->instance.freeEntities.clear();
                auto &oldBitsets = this->instance.validIndex.readComponents;
                auto &bitsets = this->instance.validIndex.writeComponents;
                for (size_t id = 0; id < bitsets.size(); id++) {
                    UpdateValidEntity<AllComponentTypes...>(id);
                    if (bitsets[id][0]) {
                        this->instance.validIndex.validEntityIndexes[id] =
                            this->instance.validIndex.writeValidEntities.size();
                        this->instance.validIndex.writeValidEntities.emplace_back(id);
                    } else {
                        this->instance.freeEntities.emplace_back(Entity(id));
                    }

                    // Compare new and old bitsets to notifiy observers
                    NotifyObservers<AllComponentTypes...>(id);
                    if (bitsets[id][0]) {
                        if (id >= oldBitsets.size() || !oldBitsets[id][0]) {
                            auto &observerList = this->instance.template Observers<EntityEvent>();
                            observerList.writeQueue->emplace_back(EventType::ADDED, Entity(id));
                        }
                    } else if (id < oldBitsets.size() && oldBitsets[id][0]) {
                        auto &observerList = this->instance.template Observers<EntityEvent>();
                        observerList.writeQueue->emplace_back(EventType::REMOVED, Entity(id));
                    }
                }
                NotifyGlobalObservers<AllComponentTypes...>();
            }
            CommitLockInOrder<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                this->instance.validIndex.CommitLock();
                this->instance.readValidGlobals = this->instance.writeValidGlobals;
                this->instance.validIndex.template CommitEntities<true>();

                CommitObservers<EntityEvent>();
                CommitObservers<ComponentEvent<AllComponentTypes>...>();
            }
            UnlockInOrder<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>()) {
                this->instance.validIndex.WriteUnlock();
            } else {
                this->instance.validIndex.ReadUnlock();
            }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            this->instance.transactionTrace.Trace(TraceEvent::Type::TransactionEnd);
#endif
        }

    private:
        // Call lock operations on Permissions in the same order they are defined in AllComponentTypes
        // This is accomplished by filtering AllComponentTypes by Permissions
        template<typename U>
        inline void CommitLockInOrder() const {
            if (is_write_allowed<U, LockType>() && this->instance.template BitsetHas<U>(this->writeAccessedFlags)) {
                this->instance.template Storage<U>().CommitLock();
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void CommitLockInOrder() const {
            CommitLockInOrder<U>();
            CommitLockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void ClearValidEntities() const {
            if constexpr (!is_global_component<U>()) {
                this->instance.template Storage<U>().writeValidEntities.clear();
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void ClearValidEntities() const {
            ClearValidEntities<U>();
            ClearValidEntities<U2, Un...>();
        }

        template<typename U>
        inline void UpdateValidEntity(size_t id) const {
            if constexpr (!is_global_component<U>()) {
                auto &bitsets = this->instance.validIndex.writeComponents;
                if (this->instance.template BitsetHas<U>(bitsets[id])) {
                    this->instance.template Storage<U>().validEntityIndexes[id] =
                        this->instance.template Storage<U>().writeValidEntities.size();
                    this->instance.template Storage<U>().writeValidEntities.emplace_back(id);
                }
            } else {
                (void)id; // Unreferenced parameter warning on MSVC
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void UpdateValidEntity(size_t id) const {
            UpdateValidEntity<U>(id);
            UpdateValidEntity<U2, Un...>(id);
        }

        template<typename U>
        inline void InitObserverEventQueues() const {
            auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
            if (!observerList.writeQueue) {
                observerList.writeQueue = std::make_shared<std::deque<ComponentEvent<U>>>();
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void InitObserverEventQueues() const {
            InitObserverEventQueues<U>();
            InitObserverEventQueues<U2, Un...>();
        }

        template<typename U>
        inline void NotifyObservers(size_t id) const {
            if constexpr (!is_global_component<U>()) {
                auto &oldBitsets = this->instance.validIndex.readComponents;
                auto &bitsets = this->instance.validIndex.writeComponents;
                if (this->instance.template BitsetHas<U>(bitsets[id])) {
                    if (id >= oldBitsets.size() || !this->instance.template BitsetHas<U>(oldBitsets[id])) {
                        auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
                        observerList.writeQueue->emplace_back(EventType::ADDED,
                            Entity(id),
                            this->instance.template Storage<U>().writeComponents[id]);
                    }
                } else if (id < oldBitsets.size() && this->instance.template BitsetHas<U>(oldBitsets[id])) {
                    auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
                    observerList.writeQueue->emplace_back(EventType::REMOVED,
                        Entity(id),
                        this->instance.template Storage<U>().readComponents[id]);
                }
            } else {
                (void)id; // Unreferenced parameter warning on MSVC
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void NotifyObservers(size_t id) const {
            NotifyObservers<U>(id);
            NotifyObservers<U2, Un...>(id);
        }

        template<typename U>
        inline void NotifyGlobalObservers() const {
            if constexpr (is_global_component<U>()) {
                auto &oldBitset = this->instance.readValidGlobals;
                auto &bitset = this->instance.writeValidGlobals;
                if (this->instance.template BitsetHas<U>(bitset)) {
                    if (!this->instance.template BitsetHas<U>(oldBitset)) {
                        auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
                        observerList.writeQueue->emplace_back(EventType::ADDED,
                            Entity(),
                            this->instance.template Storage<U>().writeComponents[0]);
                    }
                } else if (this->instance.template BitsetHas<U>(oldBitset)) {
                    auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
                    observerList.writeQueue->emplace_back(EventType::REMOVED,
                        Entity(),
                        this->instance.template Storage<U>().readComponents[0]);
                }
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void NotifyGlobalObservers() const {
            NotifyGlobalObservers<U>();
            NotifyGlobalObservers<U2, Un...>();
        }

        template<typename U>
        inline void CommitObservers() const {
            auto &observerList = this->instance.template Observers<U>();
            for (auto &observer : observerList.observers) {
                observer->insert(observer->end(), observerList.writeQueue->begin(), observerList.writeQueue->end());
            }
            observerList.writeQueue->clear();
        }

        template<typename U, typename U2, typename... Un>
        inline void CommitObservers() const {
            CommitObservers<U>();
            CommitObservers<U2, Un...>();
        }

        template<typename U>
        inline void UnlockInOrder() const {
            if (is_write_allowed<U, LockType>()) {
                if (this->instance.template BitsetHas<U>(this->writeAccessedFlags)) {
                    if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                        this->instance.template Storage<U>().template CommitEntities<true>();
                    } else {
                        this->instance.template Storage<U>().template CommitEntities<is_global_component<U>::value>();
                    }
                }
                this->instance.template Storage<U>().WriteUnlock();
            } else if (is_read_allowed<U, LockType>()) {
                this->instance.template Storage<U>().ReadUnlock();
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void UnlockInOrder() const {
            UnlockInOrder<U2, Un...>();
            UnlockInOrder<U>();
        }
    };

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
    template<typename... AllComponentTypes, typename... Permissions>
    class Lock<ECS<AllComponentTypes...>, Permissions...> {
    private:
        using LockType = Lock<ECS<AllComponentTypes...>, Permissions...>;

        ECS<AllComponentTypes...> &instance;
        std::shared_ptr<BaseTransaction<ECS, AllComponentTypes...>> base;
        std::bitset<1 + sizeof...(AllComponentTypes)> permissions;

        template<typename T>
        inline void SetPermissionBit() {
            permissions[1 + ECS<AllComponentTypes...>::template GetComponentIndex<T>()] =
                is_write_allowed<T, LockType>();
        }

    public:
        // Start a new transaction
        inline Lock(ECS<AllComponentTypes...> &instance)
            : instance(instance), base(new Transaction<ECS<AllComponentTypes...>, Permissions...>(instance)) {
            permissions[0] = is_add_remove_allowed<LockType>();
            (SetPermissionBit<AllComponentTypes>(), ...);
        }

        // Reference an existing transaction
        template<typename... PermissionsSource>
        inline Lock(const Lock<ECS<AllComponentTypes...>, PermissionsSource...> &source)
            : permissions(source.permissions), instance(source.instance), base(source.base) {
            using SourceLockType = Lock<ECS<AllComponentTypes...>, PermissionsSource...>;
            static_assert(is_add_remove_allowed<SourceLockType>() || !is_add_remove_allowed<LockType>(),
                "AddRemove permission is missing.");
            static_assert(std::conjunction<is_lock_subset<AllComponentTypes, LockType, SourceLockType>...>(),
                "Lock types are not a subset of existing permissions.");
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
            return instance.validIndex.readValidEntities;
        }

        inline constexpr const nonstd::span<Entity> Entities() const {
            if (permissions[0]) {
                return instance.validIndex.writeValidEntities;
            } else {
                return instance.validIndex.readValidEntities;
            }
        }

        inline Entity NewEntity() const {
            static_assert(is_add_remove_allowed<LockType>(), "Lock does not have AddRemove permission.");
            base->writeAccessedFlags[0] = true;

            Entity entity;
            if (instance.freeEntities.empty()) {
                // Allocate a new set of entities and components
                AllocateComponents<AllComponentTypes...>(TECS_ENTITY_ALLOCATION_BATCH_SIZE);
                entity.id = instance.validIndex.writeComponents.size();
                size_t newSize = entity.id + TECS_ENTITY_ALLOCATION_BATCH_SIZE;
                instance.validIndex.writeComponents.resize(newSize);
                instance.validIndex.validEntityIndexes.resize(newSize);

                // Add all but 1 of the new Entity ids to the free list.
                for (size_t id = 1; id < TECS_ENTITY_ALLOCATION_BATCH_SIZE; id++) {
                    instance.freeEntities.emplace_back(entity.id + id);
                }
            } else {
                entity = instance.freeEntities.front();
                instance.freeEntities.pop_front();
            }

            instance.validIndex.writeComponents[entity.id][0] = true;
            auto &validEntities = instance.validIndex.writeValidEntities;
            instance.validIndex.validEntityIndexes[entity.id] = validEntities.size();
            validEntities.emplace_back(entity);

            return entity;
        }

        template<typename... Tn>
        inline bool Has() const {
            static_assert(all_global_components<Tn...>(), "Only global components can be accessed without an Entity");

            if (permissions[0]) {
                return instance.template BitsetHas<Tn...>(instance.writeValidGlobals);
            } else {
                return instance.template BitsetHas<Tn...>(instance.readValidGlobals);
            }
        }

        template<typename... Tn>
        inline bool Had() const {
            static_assert(all_global_components<Tn...>(), "Only global components can be accessed without an Entity");

            return instance.template BitsetHas<Tn...>(instance.readValidGlobals);
        }

        template<typename T, typename ReturnType = std::conditional_t<is_write_allowed<T, LockType>::value, T, const T>>
        inline ReturnType &Get() const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");
            static_assert(is_write_allowed<T, LockType>() || std::is_const<ReturnType>(),
                "Can't get non-const reference of read only Component.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");

            if (is_write_allowed<T, LockType>()) {
                base->writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = true;
            }

            auto &validBitset = permissions[0] ? instance.writeValidGlobals : instance.readValidGlobals;
            if (!instance.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    validBitset[1 + instance.template GetComponentIndex<T>()] = true;
                    instance.template Storage<T>().writeComponents.resize(1);
                    instance.template Storage<T>().writeComponents[0] = {}; // Reset value before allowing reading.
                } else {
                    throw std::runtime_error("Missing global component of type: " + std::string(typeid(T).name()));
                }
            }
            if (permissions[1 + instance.template GetComponentIndex<T>()]) {
                return instance.template Storage<T>().writeComponents[0];
            } else {
                return instance.template Storage<T>().readComponents[0];
            }
        }

        template<typename T>
        inline const T &GetPrevious() const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");

            if (!instance.template BitsetHas<T>(instance.readValidGlobals)) {
                throw std::runtime_error("Missing global component of type: " + std::string(typeid(T).name()));
            }
            return instance.template Storage<T>().readComponents[0];
        }

        template<typename T>
        inline T &Set(T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = true;

            auto &validBitset = permissions[0] ? instance.writeValidGlobals : instance.readValidGlobals;
            if (!instance.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    validBitset[1 + instance.template GetComponentIndex<T>()] = true;
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
            base->writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = true;

            auto &validBitset = permissions[0] ? instance.writeValidGlobals : instance.readValidGlobals;
            if (!instance.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    base->writeAccessedFlags[0] = true;

                    validBitset[1 + instance.template GetComponentIndex<T>()] = true;
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

            RemoveComponents<Tn...>();
        }

        template<typename Event>
        inline Observer<ECS<AllComponentTypes...>, Event> Watch() const {
            static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to watch for ecs changes.");

            auto &observerList = instance.template Observers<Event>();
            auto &eventList = observerList.observers.emplace_back(std::make_shared<std::deque<Event>>());
            return Observer(instance, eventList);
        }

        template<typename Event>
        inline void StopWatching(Observer<ECS<AllComponentTypes...>, Event> &observer) const {
            static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to stop an observer.");
            auto eventList = observer.eventListWeak.lock();
            auto &observers = instance.template Observers<Event>().observers;
            observers.erase(std::remove(observers.begin(), observers.end(), eventList), observers.end());
            observer.eventListWeak.reset();
        }

        template<typename... PermissionsSubset>
        inline Lock<ECS<AllComponentTypes...>, PermissionsSubset...> Subset() const {
            using NewLockType = Lock<ECS<AllComponentTypes...>, PermissionsSubset...>;
            static_assert(is_add_remove_allowed<LockType>() || !is_add_remove_allowed<NewLockType>(),
                "AddRemove permission is missing.");
            static_assert(std::conjunction<is_lock_subset<AllComponentTypes, NewLockType, LockType>...>(),
                "Lock types are not a subset of existing permissions.");

            return Lock<ECS<AllComponentTypes...>, PermissionsSubset...>(*this);
        }

    private:
        template<typename T>
        inline void AllocateComponents(size_t count) const {
            if constexpr (!is_global_component<T>()) {
                base->writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = true;

                size_t newSize = instance.template Storage<T>().writeComponents.size() + count;
                instance.template Storage<T>().writeComponents.resize(newSize);
                instance.template Storage<T>().validEntityIndexes.resize(newSize);
            } else {
                (void)count; // Unreferenced parameter warning on MSVC
            }
        }

        template<typename T, typename T2, typename... Tn>
        inline void AllocateComponents(size_t count) const {
            AllocateComponents<T>(count);
            AllocateComponents<T2, Tn...>(count);
        }

        template<typename T>
        inline void RemoveComponents(const Entity &e) const {
            if constexpr (!is_global_component<T>()) { // Ignore global components
                auto &validBitset = instance.validIndex.writeComponents[e.id];
                if (instance.template BitsetHas<T>(validBitset)) {
                    base->writeAccessedFlags[0] = true;
                    base->writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = true;

                    validBitset[1 + instance.template GetComponentIndex<T>()] = false;
                    auto &compIndex = instance.template Storage<T>();
                    compIndex.writeComponents[e.id] = {};
                    size_t validIndex = compIndex.validEntityIndexes[e.id];
                    compIndex.writeValidEntities[validIndex] = Entity();
                }
            }
        }

        template<typename T>
        inline void RemoveComponents() const {
            static_assert(is_global_component<T>(), "Only global components can be removed without an Entity");

            auto &validBitset = instance.writeValidGlobals;
            if (instance.template BitsetHas<T>(validBitset)) {
                base->writeAccessedFlags[0] = true;
                base->writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = true;

                validBitset[1 + instance.template GetComponentIndex<T>()] = false;
                instance.template Storage<T>().writeComponents[0] = {};
            }
        }

        template<typename T, typename T2, typename... Tn>
        inline void RemoveComponents(const Entity &e) const {
            RemoveComponents<T>(e);
            RemoveComponents<T2, Tn...>(e);
        }

        inline void RemoveAllComponents(Entity e) const {
            (RemoveComponents<AllComponentTypes>(e), ...);
        }

        template<typename T, typename T2, typename... Tn>
        inline void RemoveComponents() const {
            RemoveComponents<T>();
            RemoveComponents<T2, Tn...>();
        }

        template<typename, typename...>
        friend class Lock;
        friend struct Entity;
    };
}; // namespace Tecs
