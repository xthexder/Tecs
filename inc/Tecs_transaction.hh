#pragma once

#include "Tecs_entity.hh"
#include "Tecs_observer.hh"
#include "Tecs_permissions.hh"
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
    #include "Tecs_tracing.hh"
#endif

#ifdef TECS_ENABLE_TRACY
    #include <cstring>
    #include <tracy/Tracy.hpp>
#endif

#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <type_traits>

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
    public:
        using PermissionBitset = std::bitset<1 + sizeof...(AllComponentTypes)>;

    private:
        using ECS = ECSType<AllComponentTypes...>;
        using EntityMetadata = typename ECS::EntityMetadata;

        ECS &instance;
#ifndef TECS_HEADER_ONLY
        size_t transactionId;
#endif

        const PermissionBitset readPermissions;
        const PermissionBitset writePermissions;
        PermissionBitset writeAccessedFlags;

    public:
        template<typename T>
        inline bool IsReadAllowed() const {
            return readPermissions[1 + instance.template GetComponentIndex<T>()];
        }

        template<typename T>
        inline bool IsWriteAllowed() const {
            return writePermissions[1 + instance.template GetComponentIndex<T>()];
        }

        inline bool IsAddRemoveAllowed() const {
            return writePermissions[0];
        }

        template<typename T>
        inline void SetAccessFlag() {
            if constexpr (std::is_same<T, AddRemove>()) {
                writeAccessedFlags[0] = true;
            } else {
                writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = true;
            }
        }

        template<typename T>
        inline void SetAccessFlag(TECS_ENTITY_INDEX_TYPE index) {
            writeAccessedFlags[1 + instance.template GetComponentIndex<T>()] = true;
            instance.template Storage<T>().AccessEntity(index);
        }

#ifndef TECS_HEADER_ONLY
        inline size_t GetTransactionId() const {
            return transactionId;
        }
#endif

        Transaction(ECS &instance, const PermissionBitset &readPermissions, const PermissionBitset &writePermissions)
            : instance(instance), readPermissions(readPermissions | writePermissions),
              writePermissions(writePermissions) {
#ifdef TECS_ENABLE_TRACY
            ZoneNamedN(tracyScope, "StartTransaction", true);
#endif
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            instance.transactionTrace.Trace(TraceEvent::Type::TransactionStart);
#endif

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

            PermissionBitset acquired;
            // Templated lambda functions for Lock/Unlock so they can be looped over at runtime.
            std::array<std::function<bool(bool)>, acquired.size()> lockFuncs = {
                [&](bool block) {
                    if (IsAddRemoveAllowed()) {
                        return instance.metadata.WriteLock(block);
                    } else {
                        return instance.metadata.ReadLock(block);
                    }
                },
                [&](bool block) {
                    if (IsWriteAllowed<AllComponentTypes>()) {
                        return instance.template Storage<AllComponentTypes>().WriteLock(block);
                    } else if (IsReadAllowed<AllComponentTypes>()) {
                        return instance.template Storage<AllComponentTypes>().ReadLock(block);
                    }
                    // This component type isn't part of the lock, skip.
                    return true;
                }...};
            std::array<std::function<void()>, acquired.size()> unlockFuncs = {
                [&]() {
                    if (IsAddRemoveAllowed()) {
                        return instance.metadata.WriteUnlock();
                    } else {
                        return instance.metadata.ReadUnlock();
                    }
                },
                [&]() {
                    if (IsWriteAllowed<AllComponentTypes>()) {
                        instance.template Storage<AllComponentTypes>().WriteUnlock();
                    } else if (IsReadAllowed<AllComponentTypes>()) {
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

            if (IsAddRemoveAllowed()) {
                // Init observer event queues
                instance.entityAddEvents.Init();
                instance.entityRemoveEvents.Init();
                ( // For each AllComponentTypes, unlock any Noop Writes or Read locks early
                    [&] {
                        auto &storage = instance.template Storage<AllComponentTypes>();
                        storage.componentAddEvents.Init();
                        storage.componentRemoveEvents.Init();
                        storage.componentModifyEvents.Init();
                    }(),
                    ...);
            }
        }
        // Delete copy constructor
        Transaction(const Transaction &) = delete;

        ~Transaction() {
#ifdef TECS_ENABLE_TRACY
            ZoneNamedN(tracyTxScope, "EndTransaction", true);
#endif
            if (IsAddRemoveAllowed() && writeAccessedFlags[0]) PreCommitAddRemoveMetadata();

            ( // For each AllComponentTypes, unlock any Noop Writes or Read locks early
                [&] {
                    if (IsWriteAllowed<AllComponentTypes>()) {
                        if (!instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) {
                            instance.template Storage<AllComponentTypes>().WriteUnlock();
                        }
                    } else if (IsReadAllowed<AllComponentTypes>()) {
                        instance.template Storage<AllComponentTypes>().ReadUnlock();
                    }
                }(),
                ...);

            ( // For each AllComponentTypes, run pre-commit event handlers
                [&] {
                    if (IsWriteAllowed<AllComponentTypes>()) PreCommitComponent<AllComponentTypes>();
                }(),
                ...);

            { // Acquire commit locks for all write-accessed components
#if defined(TECS_ENABLE_TRACY) && defined(TECS_TRACY_INCLUDE_DETAILED_COMMIT)
                ZoneNamedN(tracyCommitScope1, "CommitLock", true);
#endif
                if (IsAddRemoveAllowed() && writeAccessedFlags[0]) {
                    instance.metadata.CommitLock();
                }
                ( // For each AllComponentTypes
                    [&] {
                        if (IsWriteAllowed<AllComponentTypes>() &&
                            instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) {
                            instance.template Storage<AllComponentTypes>().CommitLock();
                        }
                    }(),
                    ...);
            }
            { // Swap read and write storage, and release commit lock for all held locks
#if defined(TECS_ENABLE_TRACY) && defined(TECS_TRACY_INCLUDE_DETAILED_COMMIT)
                ZoneNamedN(tracyCommitScope2, "Commit", true);
#endif

                // Commit observers
                if (IsAddRemoveAllowed() && writeAccessedFlags[0]) {
#if defined(TECS_ENABLE_TRACY) && defined(TECS_TRACY_INCLUDE_DETAILED_COMMIT)
                    ZoneNamedN(tracyCommitScope3, "CommitEntityEvent", true);
#endif
                    instance.entityAddEvents.Commit();
                    instance.entityRemoveEvents.Commit();
                }
                ( // For each AllComponentTypes
                    [&] {
                        if (IsWriteAllowed<AllComponentTypes>() &&
                            instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) {
#if defined(TECS_ENABLE_TRACY) && defined(TECS_TRACY_INCLUDE_DETAILED_COMMIT)
                            ZoneNamedN(tracyCommitScope3, "CommitComponentEvent", true);
                            ZoneTextV(tracyCommitScope3,
                                typeid(AllComponentTypes).name(),
                                std::strlen(typeid(AllComponentTypes).name()));
#endif
                            auto &storage = instance.template Storage<AllComponentTypes>();
                            if (IsAddRemoveAllowed()) {
                                storage.componentAddEvents.Commit();
                                storage.componentRemoveEvents.Commit();
                            }
                            storage.componentModifyEvents.Commit();
                        }
                    }(),
                    ...);
                if (IsAddRemoveAllowed() && writeAccessedFlags[0]) {
                    instance.metadata.readComponents.swap(instance.metadata.writeComponents);
                    instance.metadata.readValidEntities.swap(instance.metadata.writeValidEntities);
                    instance.globalReadMetadata = instance.globalWriteMetadata;
                    instance.metadata.CommitUnlock();
                }
                ( // For each AllComponentTypes
                    [&] {
                        if (IsWriteAllowed<AllComponentTypes>()) {
                            // Skip if no write accesses were made
                            if (!instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) return;
                            auto &storage = instance.template Storage<AllComponentTypes>();

                            storage.readComponents.swap(storage.writeComponents);
                            if (IsAddRemoveAllowed() && writeAccessedFlags[0]) {
                                storage.readValidEntities.swap(storage.writeValidEntities);
                            }
                            storage.CommitUnlock();
                        }
                    }(),
                    ...);
            }

            ( // For each AllComponentTypes, reset the write storage to match read.
                [&] {
                    if (IsWriteAllowed<AllComponentTypes>()) {
#if defined(TECS_ENABLE_TRACY) && defined(TECS_TRACY_INCLUDE_DETAILED_COMMIT)
                        ZoneNamedN(tracyCommitScope3, "CopyReadComponent", true);
                        ZoneTextV(tracyCommitScope3,
                            typeid(AllComponentTypes).name(),
                            std::strlen(typeid(AllComponentTypes).name()));
#endif
                        // Skip if no write accesses were made
                        if (!instance.template BitsetHas<AllComponentTypes>(writeAccessedFlags)) return;
                        auto &storage = instance.template Storage<AllComponentTypes>();

                        if constexpr (is_global_component<AllComponentTypes>()) {
                            storage.writeComponents = storage.readComponents;
                        } else if (IsAddRemoveAllowed() && writeAccessedFlags[0]) {
                            if (storage.writeComponents.size() != storage.readComponents.size()) {
                                storage.writeComponents.resize(storage.readComponents.size());
                            }
                            for (auto &index : storage.writeAccessedEntities) {
                                if (index >= storage.writeComponents.size()) continue;
                                storage.writeComponents[index] = storage.readComponents[index];
                            }
                            storage.writeValidEntities = storage.readValidEntities;
                        } else {
                            for (auto &index : storage.writeAccessedEntities) {
                                storage.writeComponents[index] = storage.readComponents[index];
                            }
                        }
                        storage.WriteUnlock();
                    }
                }(),
                ...);
            if (IsAddRemoveAllowed()) {
                if (writeAccessedFlags[0]) {
                    instance.metadata.writeComponents = instance.metadata.readComponents;
                    instance.metadata.writeValidEntities = instance.metadata.readValidEntities;
                }
                instance.metadata.WriteUnlock();
            } else {
                instance.metadata.ReadUnlock();
            }

#ifndef TECS_HEADER_ONLY
            auto start = activeTransactions.begin();
            activeTransactionsCount = std::remove(start, start + activeTransactionsCount, instance.ecsId) - start;
#endif
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            instance.transactionTrace.Trace(TraceEvent::Type::TransactionEnd);
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
                    if (oldMetadata[0]) {
                        instance.entityRemoveEvents.AddEvent(EventType::REMOVED, Entity(index, oldMetadata.generation));
                    }
                    if (newMetadata[0]) {
                        instance.entityAddEvents.AddEvent(EventType::ADDED, Entity(index, newMetadata.generation));
                    }
                }
            }
        }

        template<typename T>
        struct is_equals_comparable : std::false_type {};
#if __cpp_lib_concepts
        template<std::equality_comparable T>
        struct is_equals_comparable<T> : std::true_type {};
#endif

        template<typename U>
        inline void PreCommitComponent() const {
#if defined(TECS_ENABLE_TRACY) && defined(TECS_TRACY_INCLUDE_DETAILED_COMMIT)
            ZoneNamedN(tracyPreCommitScope, "PreCommitComponent", true);
#endif
            auto &storage = instance.template Storage<U>();
            if constexpr (is_global_component<U>()) {
                if (writeAccessedFlags[0]) {
                    const auto &oldMetadata = instance.globalReadMetadata;
                    const auto &newMetadata = instance.globalWriteMetadata;
                    if (instance.template BitsetHas<U>(newMetadata)) {
                        if (!instance.template BitsetHas<U>(oldMetadata)) {
                            storage.componentAddEvents.AddEvent(EventType::ADDED, Entity(), storage.writeComponents[0]);
                        }
                    } else if (instance.template BitsetHas<U>(oldMetadata)) {
                        storage.componentRemoveEvents.AddEvent(EventType::REMOVED, Entity(), storage.readComponents[0]);
                    }
                }
                if (instance.template BitsetHas<U>(writeAccessedFlags)) {
                    const auto &newMetadata =
                        IsAddRemoveAllowed() ? instance.globalWriteMetadata : instance.globalReadMetadata;
                    const auto &oldMetadata = instance.globalReadMetadata;
                    if (instance.template BitsetHas<U>(newMetadata) && instance.template BitsetHas<U>(oldMetadata)) {
                        if constexpr (is_equals_comparable<U>()) {
                            if (storage.writeComponents[0] != storage.readComponents[0]) {
                                storage.componentModifyEvents.AddEvent();
                            }
                        } else {
                            storage.componentModifyEvents.AddEvent();
                        }
                    }
                }
            } else {
                if (writeAccessedFlags[0]) {
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
                            if (oldExists) {
                                storage.componentRemoveEvents.AddEvent(EventType::REMOVED,
                                    Entity(index, oldMetadata.generation),
                                    storage.readComponents[index]);
                            }
                            if (newExists) {
                                storage.componentAddEvents.AddEvent(EventType::ADDED,
                                    Entity(index, newMetadata.generation),
                                    storage.writeComponents[index]);
                            }
                        }
                    }
                }
                if (instance.template BitsetHas<U>(writeAccessedFlags)) {
                    for (auto &index : storage.writeAccessedEntities) {
                        const auto &newMetadata = IsAddRemoveAllowed() ? instance.metadata.writeComponents[index]
                                                                       : instance.metadata.readComponents[index];
                        const auto &oldMetadata = index >= instance.metadata.readComponents.size()
                                                      ? emptyMetadata
                                                      : instance.metadata.readComponents[index];
                        if (instance.template BitsetHas<U>(newMetadata) &&
                            instance.template BitsetHas<U>(oldMetadata)) {
                            if constexpr (is_equals_comparable<U>()) {
                                if (storage.writeComponents[index] == storage.readComponents[index]) continue;
                            }
                            storage.componentModifyEvents.AddEvent(Entity(index, newMetadata.generation));
                        }
                    }
                }
            }
        }

#ifdef TECS_ENABLE_TRACY
        const std::string permissionsStr = [&]() {
            ZoneScopedN("PermissionsStrGen");
            std::stringstream out, write, read;
            out << "Permissions<";
            if (IsAddRemoveAllowed()) {
                out << "AddRemove";
            } else if (writePermissions.all()) {
                out << "WriteAll";
            } else {
                bool firstOut = true, firstRead = true, firstWrite = true;
                for (size_t i = 1; i <= sizeof...(AllComponentTypes); i++) {
                    if (writePermissions[i]) {
                        if (firstWrite) {
                            firstWrite = false;
                        } else {
                            write << ", ";
                        }
                        write << ECS::GetComponentName(i - 1);
                    }
                }
                if (!firstWrite) {
                    if (firstOut) {
                        firstOut = false;
                    } else {
                        out << ", ";
                    }
                    out << "Write<" << write.str() << ">";
                }
                if (readPermissions.all()) {
                    if (firstOut) {
                        firstOut = false;
                    } else {
                        out << ", ";
                    }
                    out << "ReadAll";
                } else {
                    for (size_t i = 1; i <= sizeof...(AllComponentTypes); i++) {
                        if (readPermissions[i] && !writePermissions[i]) {
                            if (firstRead) {
                                firstRead = false;
                            } else {
                                read << ", ";
                            }
                            read << ECS::GetComponentName(i - 1);
                        }
                    }
                    if (!firstRead) {
                        if (firstOut) {
                            firstOut = false;
                        } else {
                            out << ", ";
                        }
                        out << "Read<" << read.str() << ">";
                    }
                }
            }
            out << ">";
            return out.str();
        }();
    #if defined(TRACY_HAS_CALLSTACK) && defined(TRACY_CALLSTACK)
        tracy::ScopedZone tracyZone{__LINE__,
            __FILE__,
            strlen(__FILE__),
            permissionsStr.c_str(),
            permissionsStr.size(),
            "TecsTransaction",
            strlen("TecsTransaction"),
            TRACY_CALLSTACK,
            true};
    #else
        tracy::ScopedZone tracyZone{__LINE__,
            __FILE__,
            strlen(__FILE__),
            permissionsStr.c_str(),
            permissionsStr.size(),
            "TecsTransaction",
            strlen("TecsTransaction"),
            true};
    #endif
#endif
    };
} // namespace Tecs
