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
    class BaseTransaction {
    public:
        BaseTransaction(ECSType<AllComponentTypes...> &instance) : instance(instance) {
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
#ifndef TECS_HEADER_ONLY
        size_t transactionId;
#endif

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
        using FlatPermissions = typename FlattenPermissions<LockType, AllComponentTypes...>::type;

#ifdef TECS_ENABLE_TRACY
        static inline const auto tracyCtx = []() -> const tracy::SourceLocationData * {
            static const tracy::SourceLocationData srcloc{"TecsTransaction",
                FlatPermissions::Name(),
                __FILE__,
                __LINE__,
                0};
            return &srcloc;
        };
    #if defined(TRACY_HAS_CALLSTACK) && defined(TRACY_CALLSTACK)
        tracy::ScopedZone tracyZone{tracyCtx(), TRACY_CALLSTACK, true};
    #else
        tracy::ScopedZone tracyZone{tracyCtx(), true};
    #endif
#endif

    public:
        inline Transaction(ECS<AllComponentTypes...> &instance) : BaseTransaction<ECS, AllComponentTypes...>(instance) {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            TECS_EXTERNAL_TRACE_TRANSACTION_STARTING(FlatPermissions::Name());
            instance.transactionTrace.Trace(TraceEvent::Type::TransactionStart);
#endif
#ifdef TECS_ENABLE_TRACY
            ZoneNamedN(tracyScope, "StartTransaction", true);
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
            TECS_EXTERNAL_TRACE_TRANSACTION_STARTED(FlatPermissions::Name());
#endif
        }

        inline ~Transaction() {
#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            TECS_EXTERNAL_TRACE_TRANSACTION_ENDING(FlatPermissions::Name());
#endif
#ifdef TECS_ENABLE_TRACY
            ZoneNamedN(tracyTxScope, "EndTransaction", true);
#endif
            if constexpr (is_add_remove_allowed<LockType>()) {
                if (this->writeAccessedFlags[0]) {
                    PreCommitAddRemoveMetadata();
                    (PreCommitAddRemove<AllComponentTypes>(), ...);
                }
            }

            ( // For each AllComponentTypes, unlock any Noop Writes or Read locks early
                [&] {
                    if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
                        if (!this->instance.template BitsetHas<AllComponentTypes>(this->writeAccessedFlags)) {
                            this->instance.template Storage<AllComponentTypes>().WriteUnlock();
                        }
                    } else if constexpr (is_read_allowed<AllComponentTypes, LockType>()) {
                        this->instance.template Storage<AllComponentTypes>().ReadUnlock();
                    }
                }(),
                ...);

            { // Acquire commit locks for all write-accessed components
#ifdef TECS_ENABLE_TRACY
                ZoneNamedN(tracyCommitScope1, "CommitLock", true);
#endif
                if constexpr (is_add_remove_allowed<LockType>()) {
                    if (this->writeAccessedFlags[0]) this->instance.metadata.CommitLock();
                }
                ( // For each AllComponentTypes
                    [&] {
                        if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
                            if (this->instance.template BitsetHas<AllComponentTypes>(this->writeAccessedFlags)) {
                                this->instance.template Storage<AllComponentTypes>().CommitLock();
                            }
                        }
                    }(),
                    ...);
            }
            { // Swap read and write storage, and release commit lock for all held locks
#ifdef TECS_ENABLE_TRACY
                ZoneNamedN(tracyCommitScope2, "Commit", true);
#endif
                if constexpr (is_add_remove_allowed<LockType>()) {
                    if (this->writeAccessedFlags[0]) {
                        // Commit observers
                        std::apply(
                            [](auto &...args) {
                                (args.Commit(), ...);
                            },
                            this->instance.eventLists);

                        this->instance.metadata.readComponents.swap(this->instance.metadata.writeComponents);
                        this->instance.metadata.readValidEntities.swap(this->instance.metadata.writeValidEntities);
                        this->instance.globalReadMetadata = this->instance.globalWriteMetadata;
                        this->instance.metadata.CommitUnlock();
                    }
                }
                ( // For each AllComponentTypes
                    [&] {
                        if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
                            // Skip if no write accesses were made
                            if (!this->instance.template BitsetHas<AllComponentTypes>(this->writeAccessedFlags)) return;
                            auto &storage = this->instance.template Storage<AllComponentTypes>();

                            storage.readComponents.swap(storage.writeComponents);
                            if constexpr (is_add_remove_allowed<LockType>()) {
                                if (this->writeAccessedFlags[0]) {
                                    storage.readValidEntities.swap(storage.writeValidEntities);
                                }
                            }
                            storage.CommitUnlock();
                        }
                    }(),
                    ...);
            }

            ( // For each AllComponentTypes, reset the write storage to match read.
                [&] {
                    if constexpr (is_write_allowed<AllComponentTypes, LockType>()) {
#ifdef TECS_ENABLE_TRACY
                        ZoneNamedN(tracyCommitScope3, "CopyRead", true);
                        ZoneTextV(tracyCommitScope3,
                            typeid(AllComponentTypes).name(),
                            std::strlen(typeid(AllComponentTypes).name()));
#endif
                        // Skip if no write accesses were made
                        if (!this->instance.template BitsetHas<AllComponentTypes>(this->writeAccessedFlags)) return;
                        auto &storage = this->instance.template Storage<AllComponentTypes>();

                        if constexpr (is_global_component<AllComponentTypes>()) {
                            storage.writeComponents = storage.readComponents;
                        } else if (is_add_remove_allowed<LockType>() && this->writeAccessedFlags[0]) {
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
            if constexpr (is_add_remove_allowed<LockType>()) {
                if (this->writeAccessedFlags[0]) {
                    this->instance.metadata.writeComponents = this->instance.metadata.readComponents;
                    this->instance.metadata.writeValidEntities = this->instance.metadata.readValidEntities;
                }
            }
            if constexpr (is_add_remove_allowed<LockType>()) {
                this->instance.metadata.WriteUnlock();
            } else {
                this->instance.metadata.ReadUnlock();
            }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
            TECS_EXTERNAL_TRACE_TRANSACTION_ENDED(FlatPermissions::Name());
            this->instance.transactionTrace.Trace(TraceEvent::Type::TransactionEnd);
#endif
        }

    private:
        inline static const EntityMetadata emptyMetadata = {};

        inline void PreCommitAddRemoveMetadata() const {
            // Rebuild writeValidEntities, validEntityIndexes, and freeEntities with the new entity set.
            this->instance.metadata.writeValidEntities.clear();
            this->instance.freeEntities.clear();

            const auto &writeMetadataList = this->instance.metadata.writeComponents;
            for (TECS_ENTITY_INDEX_TYPE index = 0; index < writeMetadataList.size(); index++) {
                const auto &newMetadata = writeMetadataList[index];
                const auto &oldMetadata = index >= this->instance.metadata.readComponents.size()
                                              ? emptyMetadata
                                              : this->instance.metadata.readComponents[index];

                // If this index exists, add it to the valid entity lists.
                if (newMetadata[0]) {
                    this->instance.metadata.validEntityIndexes[index] =
                        this->instance.metadata.writeValidEntities.size();
                    this->instance.metadata.writeValidEntities.emplace_back(index, newMetadata.generation);
                } else {
                    this->instance.freeEntities.emplace_back(index,
                        newMetadata.generation + 1,
                        (TECS_ENTITY_ECS_IDENTIFIER_TYPE)this->instance.ecsId);
                }

                // Compare new and old metadata to notify observers
                if (newMetadata[0] != oldMetadata[0] || newMetadata.generation != oldMetadata.generation) {
                    auto &observerList = this->instance.template Observers<EntityEvent>();
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
                const auto &oldMetadata = this->instance.globalReadMetadata;
                const auto &newMetadata = this->instance.globalWriteMetadata;
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
            } else {
                auto &storage = this->instance.template Storage<U>();

                // Rebuild writeValidEntities and validEntityIndexes with the new entity set.
                storage.writeValidEntities.clear();

                const auto &writeMetadataList = this->instance.metadata.writeComponents;
                for (TECS_ENTITY_INDEX_TYPE index = 0; index < writeMetadataList.size(); index++) {
                    const auto &newMetadata = writeMetadataList[index];
                    const auto &oldMetadata = index >= this->instance.metadata.readComponents.size()
                                                  ? emptyMetadata
                                                  : this->instance.metadata.readComponents[index];

                    // If this index exists, add it to the valid entity lists.
                    if (newMetadata[0] && this->instance.template BitsetHas<U>(newMetadata)) {

                        storage.validEntityIndexes[index] = storage.writeValidEntities.size();
                        storage.writeValidEntities.emplace_back(index, newMetadata.generation);
                    }

                    // Compare new and old metadata to notify observers
                    bool newExists = this->instance.template BitsetHas<U>(newMetadata);
                    bool oldExists = this->instance.template BitsetHas<U>(oldMetadata);
                    if (newExists != oldExists || newMetadata.generation != oldMetadata.generation) {
                        auto &observerList = this->instance.template Observers<ComponentEvent<U>>();
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
    };
}; // namespace Tecs
