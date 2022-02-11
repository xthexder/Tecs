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

        template<typename T>
        inline void SetAccessFlag(bool value) {
            writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = value;
        }

        template<typename, typename...>
        friend class Lock;
        friend struct Entity;
    };

    template<typename... AllComponentTypes, typename... Permissions>
    class Transaction<ECS<AllComponentTypes...>, Permissions...> : public BaseTransaction<ECS, AllComponentTypes...> {
    private:
        using LockType = Lock<ECS<AllComponentTypes...>, Permissions...>;
        using EntityMetadata = typename ECS<AllComponentTypes...>::EntityMetadata;

    public:
        inline Transaction(ECS<AllComponentTypes...> &instance) : BaseTransaction<ECS, AllComponentTypes...>(instance) {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            TECS_EXTERNAL_TRACE_TRANSACTION_STARTING(TransactionPermissions<Permissions...>::Name());
            instance.transactionTrace.Trace(TraceEvent::Type::TransactionStart);
#endif

            std::bitset<1 + sizeof...(AllComponentTypes)> acquired;
            // Templated lambda functions for Lock/Unlock so they can be looped over at runtime.
            std::array<std::function<bool(bool)>, acquired.size()> lockFuncs = {
                [&instance](bool block) {
                    if (is_add_remove_allowed<LockType>()) {
                        return instance.metadata.WriteLock(block);
                    } else {
                        return instance.metadata.ReadLock(block);
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
                        return instance.metadata.WriteUnlock();
                    } else {
                        return instance.metadata.ReadUnlock();
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

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            TECS_EXTERNAL_TRACE_TRANSACTION_STARTED(TransactionPermissions<Permissions...>::Name());
#endif
        }

        inline ~Transaction() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            TECS_EXTERNAL_TRACE_TRANSACTION_ENDING(TransactionPermissions<Permissions...>::Name());
#endif
            if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                // Rebuild writeValidEntities, validEntityIndexes, and freeEntities with the new entity set.
                this->instance.metadata.writeValidEntities.clear();
                (ClearValidEntities<AllComponentTypes>(), ...);
                this->instance.freeEntities.clear();

                static const EntityMetadata emptyMetadata = {};
                auto &writeMetadataList = this->instance.metadata.writeComponents;
                for (TECS_ENTITY_INDEX_TYPE index = 0; index < writeMetadataList.size(); index++) {
                    auto &newMetadata = writeMetadataList[index];
                    auto &oldMetadata = index >= this->instance.metadata.readComponents.size()
                                            ? emptyMetadata
                                            : this->instance.metadata.readComponents[index];
                    (UpdateValidEntity<AllComponentTypes>(newMetadata, index), ...);
                    if (newMetadata[0]) {
                        this->instance.metadata.validEntityIndexes[index] =
                            this->instance.metadata.writeValidEntities.size();
                        this->instance.metadata.writeValidEntities.emplace_back(index, newMetadata.generation);
                    } else {
                        this->instance.freeEntities.emplace_back(index, newMetadata.generation + 1);
                    }

                    // Compare new and old metadata to notify observers
                    (NotifyObservers<AllComponentTypes>(newMetadata, oldMetadata, index), ...);
                    if (newMetadata[0] != oldMetadata[0] || newMetadata.generation != oldMetadata.generation) {
                        auto &observerList = this->instance.template Observers<EntityEvent>();
                        if (oldMetadata[0]) {
                            observerList.writeQueue->emplace_back(EventType::REMOVED,
                                Entity(index, oldMetadata.generation));
                        }
                        if (newMetadata[0]) {
                            observerList.writeQueue->emplace_back(EventType::ADDED,
                                Entity(index, newMetadata.generation));
                        }
                    }
                }
                (NotifyGlobalObservers<AllComponentTypes>(), ...);
            }
            UnlockIfNoCommit<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                this->instance.metadata.CommitLock();
            }
            CommitLockInOrder<AllComponentTypes...>();
            CommitUnlockInOrder<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
                this->instance.globalReadMetadata = this->instance.globalWriteMetadata;
                this->instance.metadata.template CommitEntities<true>();

                // Commit observers
                std::apply(
                    [](auto &...args) {
                        (args.Commit(), ...);
                    },
                    this->instance.eventLists);
            }
            if (is_add_remove_allowed<LockType>()) {
                this->instance.metadata.WriteUnlock();
            } else {
                this->instance.metadata.ReadUnlock();
            }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            TECS_EXTERNAL_TRACE_TRANSACTION_ENDED(TransactionPermissions<Permissions...>::Name());
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
        inline void UpdateValidEntity(const EntityMetadata &metadata, TECS_ENTITY_INDEX_TYPE index) const {
            if constexpr (!is_global_component<U>()) {
                if (this->instance.template BitsetHas<U>(metadata)) {
                    this->instance.template Storage<U>().validEntityIndexes[index] =
                        this->instance.template Storage<U>().writeValidEntities.size();
                    this->instance.template Storage<U>().writeValidEntities.emplace_back(index, metadata.generation);
                }
            } else {
                (void)metadata; // Unreferenced parameter warning on MSVC
                (void)index;
            }
        }

        template<typename U>
        inline void NotifyObservers(const EntityMetadata &newMetadata, const EntityMetadata &oldMetadata,
            TECS_ENTITY_INDEX_TYPE index) const {
            if constexpr (!is_global_component<U>()) {
                bool newExists = this->instance.template BitsetHas<U>(newMetadata);
                bool oldExists = this->instance.template BitsetHas<U>(oldMetadata);
                if (newExists != oldExists || newMetadata.generation != oldMetadata.generation) {
                    auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
                    if (oldExists) {
                        observerList.writeQueue->emplace_back(EventType::REMOVED,
                            Entity(index, oldMetadata.generation),
                            this->instance.template Storage<U>().readComponents[index]);
                    }
                    if (newExists) {
                        observerList.writeQueue->emplace_back(EventType::ADDED,
                            Entity(index, newMetadata.generation),
                            this->instance.template Storage<U>().writeComponents[index]);
                    }
                }
            } else {
                (void)newMetadata; // Unreferenced parameter warning on MSVC
                (void)oldMetadata;
                (void)index;
            }
        }

        template<typename U>
        inline void NotifyGlobalObservers() const {
            if constexpr (is_global_component<U>()) {
                auto &oldMetadata = this->instance.globalReadMetadata;
                auto &newMetadata = this->instance.globalWriteMetadata;
                if (this->instance.template BitsetHas<U>(newMetadata)) {
                    if (!this->instance.template BitsetHas<U>(oldMetadata)) {
                        auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
                        observerList.writeQueue->emplace_back(EventType::ADDED,
                            Entity(),
                            this->instance.template Storage<U>().writeComponents[0]);
                    }
                } else if (this->instance.template BitsetHas<U>(oldMetadata)) {
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
