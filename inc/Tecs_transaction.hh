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

#ifdef TECS_ENABLE_TRACY
        std::optional<tracy::ScopedZone> tracyZone;
#endif

        ECS &instance;
#ifndef TECS_HEADER_ONLY
        size_t transactionId;
#endif

        std::bitset<1 + sizeof...(AllComponentTypes)> readPermissions, writePermissions, writeAccessedFlags;
        std::array<uint32_t, 1 + sizeof...(AllComponentTypes)> readLockReferences, writeLockReferences;

    public:
        template<typename LockType>
        inline Transaction(ECS &instance, const LockType &)
            : instance(instance), readLockReferences({0}), writeLockReferences({0}) {
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

            readPermissions[0] = true;
            writePermissions[0] = is_add_remove_allowed<LockType>();
            // clang-format off
            ((
                readPermissions[1 + instance.template GetComponentIndex<AllComponentTypes>()] = is_read_allowed<AllComponentTypes, LockType>()
            ), ...);
            ((
                writePermissions[1 + instance.template GetComponentIndex<AllComponentTypes>()] = is_write_allowed<AllComponentTypes, LockType>()
            ), ...);
            // clang-format on

            std::bitset<1 + sizeof...(AllComponentTypes)> acquired;
            // Templated lambda functions for Lock/Unlock so they can be looped over at runtime.
            std::array<std::function<bool(bool)>, acquired.size()> lockFuncs = {
                [&](bool block) {
                    if constexpr (is_add_remove_allowed<LockType>()) {
                        return instance.metadata.WriteLock(block);
                    } else {
                        return instance.metadata.ReadLock(block);
                    }
                },
                [&](bool block) {
                    if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
                        return instance.template Storage<AllComponentTypes>().WriteLock(block);
                    } else if constexpr (is_read_allowed<AllComponentTypes, LockType>()) {
                        return instance.template Storage<AllComponentTypes>().ReadLock(block);
                    } else {
                        (void)block; // Unreferenced parameter warning on MSVC
                        // This component type isn't part of the lock, skip.
                        return true;
                    }
                }...};
            std::array<std::function<void()>, acquired.size()> unlockFuncs = {
                [&]() {
                    if constexpr (is_add_remove_allowed<LockType>()) {
                        return instance.metadata.WriteUnlock();
                    } else {
                        return instance.metadata.ReadUnlock();
                    }
                },
                [&]() {
                    if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
                        instance.template Storage<AllComponentTypes>().WriteUnlock();
                    } else if constexpr (is_read_allowed<AllComponentTypes, LockType>()) {
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

            if constexpr (is_add_remove_allowed<LockType>()) {
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

        template<typename T>
        inline void SetAccessFlag(bool value) {
            writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = value;
        }

        template<typename LockType>
        inline void AcquireLockReference() {
            readLockReferences[0]++;
            if constexpr (is_add_remove_allowed<LockType>()) {
                if (!writePermissions[0]) throw std::runtime_error("AddRemove lock not acquired");
                writeLockReferences[0]++;
            } else if constexpr (is_add_remove_optional<LockType>()) {
                if (writePermissions[0]) writeLockReferences[0]++;
            }
            ( // For each AllComponentTypes
                [&] {
                    constexpr auto compIndex = 1 + ECS::template GetComponentIndex<AllComponentTypes>();
                    if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
                        if (!writePermissions[compIndex]) throw std::runtime_error("Write lock not acquired");
                        writeLockReferences[compIndex]++;
                    } else if constexpr (is_write_optional<AllComponentTypes, LockType>()) {
                        if (writePermissions[compIndex]) writeLockReferences[compIndex]++;
                    }
                    if constexpr (is_read_allowed<AllComponentTypes, LockType>()) {
                        if (!readPermissions[compIndex]) throw std::runtime_error("Read lock not acquired");
                        readLockReferences[compIndex]++;
                    } else if constexpr (is_read_optional<AllComponentTypes, LockType>()) {
                        if (readPermissions[compIndex]) readLockReferences[compIndex]++;
                    }
                }(),
                ...);
        }

        template<typename LockType>
        void ReleaseLockReference() {
            readLockReferences[0]--;
            if constexpr (is_add_remove_allowed<LockType>()) {
                if (!writePermissions[0]) throw std::runtime_error("AddRemove lock not acquired");
                writeLockReferences[0]--;
            } else if constexpr (is_add_remove_optional<LockType>()) {
                if (writePermissions[0]) writeLockReferences[0]--;
            }
            ( // For each AllComponentTypes
                [&] {
                    constexpr auto compIndex = 1 + ECS::template GetComponentIndex<AllComponentTypes>();
                    if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
                        if (!writePermissions[compIndex]) throw std::runtime_error("Write lock not acquired");
                        writeLockReferences[compIndex]--;
                    } else if constexpr (is_write_optional<AllComponentTypes, LockType>()) {
                        if (writePermissions[compIndex]) {
                            if (!writePermissions[compIndex]) throw std::runtime_error("Write lock not acquired");
                            writeLockReferences[compIndex]--;
                        }
                    }
                    if constexpr (is_read_allowed<AllComponentTypes, LockType>()) {
                        if (!readPermissions[compIndex]) throw std::runtime_error("Read lock not acquired");
                        readLockReferences[compIndex]--;
                    } else if constexpr (is_read_optional<AllComponentTypes, LockType>()) {
                        if (readPermissions[compIndex]) {
                            if (!readPermissions[compIndex]) throw std::runtime_error("Read lock not acquired");
                            readLockReferences[compIndex]--;
                        }
                    }

                    if (writeAccessedFlags[0] || writeAccessedFlags[compIndex]) return;
                    if (writePermissions[compIndex]) {
                        if (writeLockReferences[compIndex] == 0) {
                            writePermissions[compIndex] = false;
                            if (readLockReferences[compIndex] == 0) {
                                readPermissions[compIndex] = false;
                                instance.template Storage<AllComponentTypes>().WriteUnlock();
                            } else {
                                instance.template Storage<AllComponentTypes>().WriteToReadLock();
                            }
                        }
                    } else if (readPermissions[compIndex]) {
                        if (readLockReferences[compIndex] == 0) {
                            readPermissions[compIndex] = false;
                            instance.template Storage<AllComponentTypes>().ReadUnlock();
                        }
                    }
                }(),
                ...);
        }

        inline ~Transaction() {
#ifdef TECS_ENABLE_TRACY
            ZoneNamedN(tracyTxScope, "EndTransaction", true);
#endif
            // If an AddRemove was performed, update the entity validity metadata
            if (writeAccessedFlags[0]) {
                PreCommitAddRemoveMetadata();
                (PreCommitAddRemove<AllComponentTypes>(), ...);
            }

            ( // For each AllComponentTypes, unlock any Noop Writes or Read locks early
                [this] {
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
                    [this] {
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
                    [this] {
                        using CompType = AllComponentTypes;
                        if (instance.template BitsetHas<CompType>(writeAccessedFlags)) {
                            auto &storage = instance.template Storage<CompType>();

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
                [this] {
                    using CompType = AllComponentTypes;
                    if (instance.template BitsetHas<CompType>(writeAccessedFlags)) {
                        auto &storage = instance.template Storage<CompType>();

                        if (is_global_component<CompType>()) {
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
        template<typename, typename...>
        friend class EntityLock;
        friend struct Entity;
    };
}; // namespace Tecs
