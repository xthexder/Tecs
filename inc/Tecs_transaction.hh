#pragma once

#include "Tecs_entity.hh"
#include "Tecs_observer.hh"
#include "Tecs_permissions.hh"
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #include "Tecs_tracing.hh"
#endif

#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <stdexcept>

namespace Tecs {
#ifndef TECS_HEADER_ONLY
    #ifndef TECS_MAX_ACTIVE_TRANSACTIONS_PER_THREAD
        #define TECS_MAX_ACTIVE_TRANSACTIONS_PER_THREAD 64
    #endif

    // Used for detecting nested transactions
    extern thread_local std::array<size_t, TECS_MAX_ACTIVE_TRANSACTIONS_PER_THREAD> activeTransactions;
    extern thread_local size_t activeTransactionsCount;
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
            for (size_t i = 0; i < activeTransactionsCount; i++) {
                if (activeTransactions[i] == instance.ecsId)
                    throw std::runtime_error("Nested transactions are not allowed");
            }
            if (activeTransactionsCount == activeTransactions.size()) {
                throw std::runtime_error("A single thread can't create more than "
                                         "TECS_MAX_ACTIVE_TRANSACTIONS_PER_THREAD simultaneous transactions");
            }
            activeTransactions[activeTransactionsCount++] = instance.ecsId;
#endif
        }
        // Delete copy constructor
        BaseTransaction(const BaseTransaction &) = delete;

        virtual ~BaseTransaction() {
#ifndef TECS_HEADER_ONLY
            auto start = activeTransactions.begin();
            activeTransactionsCount = std::remove(start, start + activeTransactionsCount, instance.ecsId) - start;
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
                // Init observer event queues
                std::apply(
                    [](auto &...args) {
                        (args.Init(), ...);
                    },
                    this->instance.eventLists);
            }
        }

        inline ~Transaction() {
            if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                // Rebuild writeValidEntities, validEntityIndexes, and freeEntities with the new entity set.
                this->instance.validIndex.writeValidEntities.clear();
                (ClearValidEntities<AllComponentTypes>(), ...);
                this->instance.freeEntities.clear();
                auto &oldValidIndex = this->instance.validIndex.readComponents;
                auto &newValidIndex = this->instance.validIndex.writeComponents;
                for (size_t index = 0; index < newValidIndex.size(); index++) {
                    auto &newMetadata = newValidIndex[index];
                    auto oldMetadata = index >= oldValidIndex.size() ? EntityMetadata() : oldValidIndex[index];
                    (UpdateValidEntity<AllComponentTypes>(index), ...);
                    if (newMetadata.validComponents[0]) {
                        this->instance.validIndex.validEntityIndexes[index] =
                            this->instance.validIndex.writeValidEntities.size();
                        this->instance.validIndex.writeValidEntities.emplace_back(index, newMetadata.generation);
                    } else {
                        this->instance.freeEntities.emplace_back(index, newMetadata.generation + 1);
                    }

                    // Compare new and old metadata to notify observers
                    (NotifyObservers<AllComponentTypes>(index), ...);
                    if (newMetadata.validComponents[0]) {
                        if (!oldMetadata.validComponents[0] || oldMetadata.generation != newMetadata.generation) {
                            auto &observerList = this->instance.template Observers<EntityEvent>();
                            observerList.writeQueue->emplace_back(EventType::ADDED, Entity(EntityId(index, newMetadata.generation)));
                        }
                    } else if (oldMetadata.validComponents[0] || oldMetadata.generation != newMetadata.generation) {
                        auto &observerList = this->instance.template Observers<EntityEvent>();
                        observerList.writeQueue->emplace_back(EventType::REMOVED, Entity(EntityId(index, oldMetadata.generation)));
                    }
                }
                (NotifyGlobalObservers<AllComponentTypes>(), ...);
            }
            UnlockIfNoCommit<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                this->instance.validIndex.CommitLock();
            }
            CommitLockInOrder<AllComponentTypes...>();
            CommitUnlockInOrder<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                this->instance.readValidGlobals = this->instance.writeValidGlobals;
                this->instance.validIndex.template CommitEntities<true>();

                // Commit observers
                std::apply(
                    [](auto &...args) {
                        (args.Commit(), ...);
                    },
                    this->instance.eventLists);
            }
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
        template<typename U>
        inline void ClearValidEntities() const {
            if constexpr (!is_global_component<U>()) {
                this->instance.template Storage<U>().writeValidEntities.clear();
            }
        }

        template<typename U>
        inline void UpdateValidEntity(size_t index) const {
            if constexpr (!is_global_component<U>()) {
                auto &metadata = this->instance.validIndex.writeComponents;
                if (this->instance.template MetadataHas<U>(metadata[index])) {
                    this->instance.template Storage<U>().validEntityIndexes[index] =
                        this->instance.template Storage<U>().writeValidEntities.size();
                    this->instance.template Storage<U>().writeValidEntities.emplace_back(index, metadata.generation);
                }
            } else {
                (void)index; // Unreferenced parameter warning on MSVC
            }
        }

        template<typename U>
        inline void NotifyObservers(size_t index) const {
            if constexpr (!is_global_component<U>()) {
                auto &oldValidIndex = this->instance.validIndex.readComponents;
                auto &newMetadata = this->instance.validIndex.writeComponents[index];
                auto oldMetadata = index >= oldValidIndex.size() ? EntityMetadata() : oldValidIndex[index];
                if (this->instance.template MetadataHas<U>(newMetadata)) {
                    if (!this->instance.template MetadataHas<U>(oldMetadata)) {
                        auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
                        observerList.writeQueue->emplace_back(EventType::ADDED,
                            Entity(EntityId(index, newMetadata.generation)),
                            this->instance.template Storage<U>().writeComponents[index]);
                    }
                } else if (this->instance.template MetadataHas<U>(oldMetadata)) {
                    auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
                    observerList.writeQueue->emplace_back(EventType::REMOVED,
                        Entity(EntityId(index, oldMetadata.generation)),
                        this->instance.template Storage<U>().readComponents[index]);
                }
            } else {
                (void)index; // Unreferenced parameter warning on MSVC
            }
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

        // Call lock operations on Permissions in the same order they are defined in AllComponentTypes
        // This is accomplished by filtering AllComponentTypes by Permissions
        template<typename U, typename... Un>
        inline void UnlockIfNoCommit() const {
            if (is_write_allowed<U, LockType>()) {
                if (!this->instance.template BitsetHas<U>(this->writeAccessedFlags)) {
                    this->instance.template Storage<U>().WriteUnlock();
                }
            } else if (is_read_allowed<U, LockType>()) {
                this->instance.template Storage<U>().ReadUnlock();
            }
            if constexpr (sizeof...(Un) > 0) UnlockIfNoCommit<Un...>();
        }

        template<typename U, typename... Un>
        inline void CommitLockInOrder() const {
            if (is_write_allowed<U, LockType>() && this->instance.template BitsetHas<U>(this->writeAccessedFlags)) {
                this->instance.template Storage<U>().CommitLock();
            }
            if constexpr (sizeof...(Un) > 0) CommitLockInOrder<Un...>();
        }

        template<typename U, typename... Un>
        inline void CommitUnlockInOrder() const {
            if (is_write_allowed<U, LockType>() && this->instance.template BitsetHas<U>(this->writeAccessedFlags)) {
                if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                    this->instance.template Storage<U>().template CommitEntities<true>();
                } else {
                    this->instance.template Storage<U>().template CommitEntities<is_global_component<U>::value>();
                }
                this->instance.template Storage<U>().WriteUnlock();
            }
            if constexpr (sizeof...(Un) > 0) CommitUnlockInOrder<Un...>();
        }
    };
}; // namespace Tecs
