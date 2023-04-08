#pragma once

#include "Tecs_entity.hh"
#include "Tecs_observer.hh"
#include "Tecs_permissions.hh"
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #include "Tecs_tracing.hh"
#endif

#ifdef TECS_ENABLE_TRACY
    #include <tracy/Tracy.hpp>
#endif

#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <optional>
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
    extern std::atomic_size_t nextTransactionId;
#endif

    template<typename>
    class Transaction;

    /**
     * When a Transaction is started, the relevant parts of the ECS are locked based on the Transactions Permissons.
     * The permissions can then be referenced by passing around Lock objects.
     *
     * Upon deconstruction, a Transaction will commit any changes written during its lifespan to the instance.
     * Once a Transaction is deconstructed, all Locks referencing its permissions become invalid.
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes>
    class Transaction<ECSType<AllComponentTypes...>> {
    private:
        using ECS = ECSType<AllComponentTypes...>;
        using EntityMetadata = typename ECS::EntityMetadata;
        using ComponentBitset = typename ECS::ComponentBitset;

#ifdef TECS_ENABLE_TRACY
        std::optional<tracy::ScopedZone> tracyZone;
#endif

        ECS &instance;
#ifndef TECS_HEADER_ONLY
        size_t transactionId;
#endif

        ComponentBitset readPermissions, writePermissions, writeAccessedFlags;
        std::array<uint32_t, 1 + sizeof...(AllComponentTypes)> readLockReferences = {0};
        std::array<uint32_t, 1 + sizeof...(AllComponentTypes)> writeLockReferences = {0};

    public:
        Transaction(ECS &instance, const ComponentBitset &readPermissions, const ComponentBitset &writePermissions)
            : instance(instance), readPermissions(readPermissions), writePermissions(writePermissions) {
#ifndef TECS_HEADER_ONLY
            transactionId = ++nextTransactionId;
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

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            instance.transactionTrace.Trace(TraceEvent::Type::TransactionStart);
#endif
#ifdef TECS_ENABLE_TRACY
            ZoneNamedN(tracyScope, "StartTransaction", true);
#endif

            ComponentBitset acquired;
            static const auto noopLock = [](ECS &, bool) {
                return true;
            };
            static const auto noopUnlock = [](ECS &) {};

            // Templated lambda functions for Lock/Unlock so they can be looped over at runtime.
            static const std::array<bool (*)(ECS &, bool), acquired.size()> readLockFuncs = {
                [](ECS &instance, bool block) {
                    return instance.metadata.ReadLock(block);
                },
                [](ECS &instance, bool block) {
                    return instance.template Storage<AllComponentTypes>().ReadLock(block);
                }...};
            static const std::array<bool (*)(ECS &, bool), acquired.size()> writeLockFuncs = {
                [](ECS &instance, bool block) {
                    return instance.metadata.WriteLock(block);
                },
                [](ECS &instance, bool block) {
                    return instance.template Storage<AllComponentTypes>().WriteLock(block);
                }...};

            static const std::array<void (*)(ECS &), acquired.size()> readUnlockFuncs = {
                [](ECS &instance) {
                    instance.metadata.ReadUnlock();
                },
                [](ECS &instance) {
                    instance.template Storage<AllComponentTypes>().ReadUnlock();
                }...};
            static const std::array<void (*)(ECS &), acquired.size()> writeUnlockFuncs = {
                [](ECS &instance) {
                    instance.metadata.WriteUnlock();
                },
                [](ECS &instance) {
                    instance.template Storage<AllComponentTypes>().WriteUnlock();
                }...};

            std::array<bool (*)(ECS &, bool), acquired.size()> lockFuncs;
            std::array<void (*)(ECS &), acquired.size()> unlockFuncs;
            lockFuncs[0] = writePermissions[0] ? writeLockFuncs[0] : readLockFuncs[0];
            unlockFuncs[0] = writePermissions[0] ? writeUnlockFuncs[0] : readUnlockFuncs[0];
            for (size_t i = 1; i < acquired.size(); i++) {
                if (writePermissions[i]) {
                    lockFuncs[i] = writeLockFuncs[i];
                    unlockFuncs[i] = writeUnlockFuncs[i];
                } else if (readPermissions[i]) {
                    lockFuncs[i] = readLockFuncs[i];
                    unlockFuncs[i] = readUnlockFuncs[i];
                } else {
                    lockFuncs[i] = noopLock;
                    unlockFuncs[i] = noopUnlock;
                }
            }

            // Attempt to lock all applicable components and rollback if not all locks can be immediately acquired.
            // This should only block while no locks are held to prevent deadlocks.
            bool rollback = false;
            for (size_t i = 0; !acquired.all(); i = (i + 1) % acquired.size()) {
                if (rollback) {
                    if (acquired[i]) {
                        unlockFuncs[i](instance);
                        acquired[i] = false;
                        continue;
                    } else if (acquired.none()) {
                        rollback = false;
                    }
                }
                if (!rollback) {
                    if (lockFuncs[i](instance, acquired.none())) {
                        acquired[i] = true;
                    } else {
                        rollback = true;
                    }
                }
            }

            if (writePermissions[0]) {
                // Init observer event queues
                std::apply(
                    [](auto &...args) {
                        (args.Init(), ...);
                    },
                    instance.eventLists);
            }
        }

        // Delete copy constructor
        Transaction(const Transaction &) = delete;

        ~Transaction() {
#ifdef TECS_ENABLE_TRACY
            ZoneNamedN(tracyTxScope, "EndTransaction", true);
#endif
            // If an AddRemove was performed, update the entity validity metadata
            if (writeAccessedFlags[0]) {
                PreCommitAddRemoveMetadata();
                (PreCommitAddRemove<AllComponentTypes>(), ...);
            }

            ( // For each AllComponentTypes, unlock any Noop Writes or Read locks early
                [&] {
                    if (instance.template BitsetHas<AllComponentTypes>(writePermissions)) {
                        if (!instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) {
                            instance.template Storage<AllComponentTypes>().WriteUnlock();
                        }
                    } else if (instance.template BitsetHas<AllComponentTypes>(readPermissions)) {
                        instance.template Storage<AllComponentTypes>().ReadUnlock();
                    }
                }(),
                ...);

            { // Acquire commit locks for all write-accessed components
#ifdef TECS_ENABLE_TRACY
                ZoneNamedN(tracyCommitScope1, "CommitLock", true);
#endif
                if (writeAccessedFlags[0]) { // If AddRemove performed
                    instance.metadata.CommitLock();
                }
                ( // For each AllComponentTypes
                    [&] {
                        if (instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) {
                            instance.template Storage<AllComponentTypes>().CommitLock();
                        }
                    }(),
                    ...);
            }
            { // Swap read and write storage, and release commit lock for all held locks
#ifdef TECS_ENABLE_TRACY
                ZoneNamedN(tracyCommitScope2, "Commit", true);
#endif
                if (writeAccessedFlags[0]) { // If AddRemove performed
                    // Commit observers
                    std::apply(
                        [](auto &...args) {
                            (args.Commit(), ...);
                        },
                        instance.eventLists);

                    instance.metadata.readComponents.swap(instance.metadata.writeComponents);
                    instance.metadata.readValidEntities.swap(instance.metadata.writeValidEntities);
                    instance.globalReadMetadata = instance.globalWriteMetadata;
                    instance.metadata.CommitUnlock();
                }
                ( // For each modified component type, swap read and write storage, and release commit lock.
                    [&] {
                        if (instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) {
                            auto &storage = instance.template Storage<AllComponentTypes>();

                            storage.readComponents.swap(storage.writeComponents);
                            if (writeAccessedFlags[0]) { // If AddRemove performed
                                storage.readValidEntities.swap(storage.writeValidEntities);
                            }
                            storage.CommitUnlock();
                        }
                    }(),
                    ...);
            }

            ( // For each modified component type, reset the write storage to match read.
                [&] {
                    if (instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) {
                        auto &storage = instance.template Storage<AllComponentTypes>();

                        if constexpr (is_global_component<AllComponentTypes>()) {
                            storage.writeComponents = storage.readComponents;
                        } else if (writeAccessedFlags[0]) { // If AddRemove performed
                            storage.writeComponents = storage.readComponents;
                            storage.writeValidEntities = storage.readValidEntities;
                        } else {
                            // Based on benchmarks, it is faster to bulk copy if more than
                            // roughly 1/6 of the components are valid.
                            if (storage.readValidEntities.size() > storage.readComponents.size() / 6) {
                                storage.writeComponents = storage.readComponents;
                            } else {
                                for (auto &valid : storage.readValidEntities) {
                                    storage.writeComponents[valid.index] = storage.readComponents[valid.index];
                                }
                            }
                        }
                        storage.WriteUnlock();
                    }
                }(),
                ...);
            if (writePermissions[0]) {       // If AddRemove allowed
                if (writeAccessedFlags[0]) { // If AddRemove performed
                    instance.metadata.writeComponents = instance.metadata.readComponents;
                    instance.metadata.writeValidEntities = instance.metadata.readValidEntities;
                }
                instance.metadata.WriteUnlock();
            } else {
                instance.metadata.ReadUnlock();
            }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            instance.transactionTrace.Trace(TraceEvent::Type::TransactionEnd);
#endif
#ifndef TECS_HEADER_ONLY
            auto start = activeTransactions.begin();
            activeTransactionsCount = std::remove(start, start + activeTransactionsCount, instance.ecsId) - start;
#endif
        }

        template<typename T>
        inline void SetAccessFlag(bool value) {
            writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = value;
        }

    private:
        inline static const EntityMetadata emptyMetadata = {};

        inline void PreCommitAddRemoveMetadata() const {
            // Rebuild writeValidEntities, validEntityIndexes, and freeEntities with the new entity set.
            instance.metadata.writeValidEntities.clear();
            instance.freeEntities.clear();

            const auto &writeMetadataList = instance.metadata.writeComponents;
            for (TECS_ENTITY_INDEX_TYPE index = 0; index < writeMetadataList.size(); index++) {
                const auto &newMetadata = writeMetadataList[index];
                const auto &oldMetadata = index >= instance.metadata.readComponents.size()
                                              ? emptyMetadata
                                              : instance.metadata.readComponents[index];

                // If this index exists, add it to the valid entity lists.
                if (newMetadata[0]) {
                    instance.metadata.validEntityIndexes[index] = instance.metadata.writeValidEntities.size();
                    instance.metadata.writeValidEntities.emplace_back(index, newMetadata.generation);
                } else {
                    instance.freeEntities.emplace_back(index,
                        newMetadata.generation + 1,
                        (TECS_ENTITY_ECS_IDENTIFIER_TYPE)instance.ecsId);
                }

                // Compare new and old metadata to notify observers
                if (newMetadata[0] != oldMetadata[0] || newMetadata.generation != oldMetadata.generation) {
                    auto &observerList = instance.template Observers<EntityEvent>();
                    if (oldMetadata[0]) {
                        observerList.writeQueue->emplace_back(EventType::REMOVED,
                            Entity(index, oldMetadata.generation));
                    }
                    if (newMetadata[0]) {
                        observerList.writeQueue->emplace_back(EventType::ADDED, Entity(index, newMetadata.generation));
                    }
                }
            }
        }

        template<typename U>
        inline void PreCommitAddRemove() const {
            if constexpr (is_global_component<U>()) {
                const auto &oldMetadata = instance.globalReadMetadata;
                const auto &newMetadata = instance.globalWriteMetadata;
                if (instance.template BitsetHas<U>(newMetadata)) {
                    if (!instance.template BitsetHas<U>(oldMetadata)) {
                        auto &observerList = instance.template Observers<ComponentEvent<U>>();
                        observerList.writeQueue->emplace_back(EventType::ADDED,
                            Entity(),
                            instance.template Storage<U>().writeComponents[0]);
                    }
                } else if (instance.template BitsetHas<U>(oldMetadata)) {
                    auto &observerList = instance.template Observers<ComponentEvent<U>>();
                    observerList.writeQueue->emplace_back(EventType::REMOVED,
                        Entity(),
                        instance.template Storage<U>().readComponents[0]);
                }
            } else {
                auto &storage = instance.template Storage<U>();

                // Rebuild writeValidEntities and validEntityIndexes with the new entity set.
                storage.writeValidEntities.clear();

                const auto &writeMetadataList = instance.metadata.writeComponents;
                for (TECS_ENTITY_INDEX_TYPE index = 0; index < writeMetadataList.size(); index++) {
                    const auto &newMetadata = writeMetadataList[index];
                    const auto &oldMetadata = index >= instance.metadata.readComponents.size()
                                                  ? emptyMetadata
                                                  : instance.metadata.readComponents[index];

                    // If this index exists, add it to the valid entity lists.
                    if (newMetadata[0] && instance.template BitsetHas<U>(newMetadata)) {

                        storage.validEntityIndexes[index] = storage.writeValidEntities.size();
                        storage.writeValidEntities.emplace_back(index, newMetadata.generation);
                    }

                    // Compare new and old metadata to notify observers
                    bool newExists = instance.template BitsetHas<U>(newMetadata);
                    bool oldExists = instance.template BitsetHas<U>(oldMetadata);
                    if (newExists != oldExists || newMetadata.generation != oldMetadata.generation) {
                        auto &observerList = instance.template Observers<ComponentEvent<U>>();
                        if (oldExists) {
                            observerList.writeQueue->emplace_back(EventType::REMOVED,
                                Entity(index, oldMetadata.generation),
                                storage.readComponents[index]);
                        }
                        if (newExists) {
                            observerList.writeQueue->emplace_back(EventType::ADDED,
                                Entity(index, newMetadata.generation),
                                storage.writeComponents[index]);
                        }
                    }
                }
            }
        }

        template<typename, typename...>
        friend class Lock;
        template<typename, typename>
        friend class LockImpl;
        friend struct Entity;
    };
} // namespace Tecs
