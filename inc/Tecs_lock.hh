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
    class Lock;
    template<typename, typename>
    class LockImpl;

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
    class Lock<ECSType<AllComponentTypes...>, Permissions...>
        : public LockImpl<ECSType<AllComponentTypes...>,
              SortPermissions<TransactionPermissions<Permissions...>, AllComponentTypes...>> {
    private:
        using ECS = ECSType<AllComponentTypes...>;
        using LockType = Lock<ECS, Permissions...>;
        using FlatPermissions = SortPermissions<TransactionPermissions<Permissions...>, AllComponentTypes...>;
        using ImplType = LockImpl<ECS, FlatPermissions>;

    public:
        // Start a new transaction
        inline Lock(ECS &instance)
            : ImplType(instance,
                  std::make_shared<Transaction<ECS>>(instance, ECS::template ReadBitset<FlatPermissions>(),
                      ECS::template WriteBitset<FlatPermissions>()),
                  this->base->writePermissions & ImplType::AcquireReadBitset()) {}

        // Reference an identical lock
        inline Lock(const LockType &source) : ImplType(source.instance, source.base, source.readAliasesWriteStorage) {}
        inline Lock(const ImplType &impl) : ImplType(impl) {}

        // Reference a subset of an existing transaction
        template<typename SourcePermissions,
            std::enable_if_t<ImplType::template is_lock_subset<SourcePermissions>(), int> = 0>
        Lock(const LockImpl<ECS, SourcePermissions> &other)
            : ImplType(other.instance, other.base, other.base->writePermissions & ImplType::AcquireReadBitset()) {}
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename Permissions>
    class LockImpl<ECSType<AllComponentTypes...>, Permissions> {
    protected:
        using ECS = ECSType<AllComponentTypes...>;
        using ComponentBitset = typename ECS::ComponentBitset;

        ECS &instance;
        const std::shared_ptr<Transaction<ECS>> base;
        const ComponentBitset readAliasesWriteStorage;

        LockImpl(ECS &instance, const std::shared_ptr<Transaction<ECS>> &base, const ComponentBitset &readAliases)
            : instance(instance), base(base), readAliasesWriteStorage(readAliases) {
            AcquireLockReference();
        }

        ~LockImpl() {
            ReleaseLockReference();
        }

    public:
        // Returns true if this lock type can be constructed from a lock with the specified source permissions
        template<typename SourcePermissions>
        static constexpr bool is_lock_subset() {
            if constexpr (is_add_remove_allowed<Permissions>() && !is_add_remove_allowed<SourcePermissions>()) {
                return false;
            } else {
                return std::conjunction<Tecs::is_lock_subset<AllComponentTypes, Permissions, SourcePermissions>...>();
            }
        }

        // Reference an identical lock
        LockImpl(const LockImpl &source)
            : instance(source.instance), base(source.base), readAliasesWriteStorage(source.readAliasesWriteStorage) {
            AcquireLockReference();
        }

        // Reference a subset of an existing transaction
        template<typename SourcePermissions, std::enable_if_t<is_lock_subset<SourcePermissions>(), int> = 0>
        LockImpl(const LockImpl<ECS, SourcePermissions> &other)
            : instance(other.instance), base(other.base),
              readAliasesWriteStorage(other.base->writePermissions & AcquireReadBitset()) {
            AcquireLockReference();
        }

        // Returns true if this lock type has all of the requested permissions
        template<typename RequestedPermissions>
        static constexpr bool has_permissions() {
            if constexpr (is_add_remove_allowed<RequestedPermissions>() && !is_add_remove_allowed<Permissions>()) {
                return false;
            } else {
                return std::conjunction<
                    Tecs::is_lock_subset<AllComponentTypes, RequestedPermissions, Permissions>...>();
            }
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

            if (readAliasesWriteStorage[0]) {
                return instance.template Storage<T>().writeValidEntities;
            } else {
                return instance.template Storage<T>().readValidEntities;
            }
        }

        inline const EntityView PreviousEntities() const {
            return instance.metadata.readValidEntities;
        }

        inline const EntityView Entities() const {
            if (readAliasesWriteStorage[0]) {
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
            static_assert(is_add_remove_allowed<Permissions>(), "Lock does not have AddRemove permission.");
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

            if (readAliasesWriteStorage[0]) {
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

        template<typename T, typename ReturnType = std::conditional_t<
                                 is_write_allowed<std::remove_cv_t<T>, Permissions>::value, T, const T>>
        inline ReturnType &Get() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, Permissions>(), "Component is not locked for reading.");
            static_assert(is_write_allowed<CompType, Permissions>() || std::is_const<ReturnType>(),
                "Can't get non-const reference of read only Component.");
            static_assert(is_global_component<CompType>(), "Only global components can be accessed without an Entity");

            if (!std::is_const<ReturnType>()) base->template SetAccessFlag<CompType>(true);

            auto &metadata = readAliasesWriteStorage[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<CompType>(metadata)) {
                if (is_add_remove_allowed<Permissions>()) {
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
            if (instance.template BitsetHas<CompType>(readAliasesWriteStorage)) {
                return instance.template Storage<CompType>().writeComponents[0];
            } else {
                return instance.template Storage<CompType>().readComponents[0];
            }
        }

        template<typename T>
        inline const T &GetPrevious() const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, Permissions>(), "Component is not locked for reading.");
            static_assert(is_global_component<CompType>(), "Only global components can be accessed without an Entity");

            if (!instance.template BitsetHas<CompType>(instance.globalReadMetadata)) {
                throw std::runtime_error("Missing global component of type: " + std::string(typeid(CompType).name()));
            }
            return instance.template Storage<CompType>().readComponents[0];
        }

        template<typename T>
        inline T &Set(T &value) const {
            static_assert(is_write_allowed<T, Permissions>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->template SetAccessFlag<T>(true);

            auto &metadata = readAliasesWriteStorage[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<Permissions>()) {
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
            static_assert(is_write_allowed<T, Permissions>(), "Component is not locked for writing.");
            static_assert(is_global_component<T>(), "Only global components can be accessed without an Entity");
            base->template SetAccessFlag<T>(true);

            auto &metadata = readAliasesWriteStorage[0] ? instance.globalWriteMetadata : instance.globalReadMetadata;
            if (!instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<Permissions>()) {
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
            static_assert(is_add_remove_allowed<Permissions>(),
                "Components cannot be removed without an AddRemove lock.");
            static_assert(all_global_components<Tn...>(), "Only global components can be unset without an Entity");

            (RemoveComponents<Tn>(), ...);
        }

        template<typename Event>
        inline Observer<ECS, Event> Watch() const {
            static_assert(is_add_remove_allowed<Permissions>(),
                "An AddRemove lock is required to watch for ecs changes.");

            auto &observerList = instance.template Observers<Event>();
            auto &eventList = observerList.observers.emplace_back(std::make_shared<std::deque<Event>>());
            return Observer(instance, eventList);
        }

        template<typename Event>
        inline void StopWatching(Observer<ECS, Event> &observer) const {
            static_assert(is_add_remove_allowed<Permissions>(), "An AddRemove lock is required to stop an observer.");
            auto eventList = observer.eventListWeak.lock();
            auto &observers = instance.template Observers<Event>().observers;
            observers.erase(std::remove(observers.begin(), observers.end(), eventList), observers.end());
            observer.eventListWeak.reset();
        }

        template<typename PermissionsSubset>
        inline Lock<ECS, PermissionsSubset> Subset() const {
            static_assert(has_permissions<PermissionsSubset>(), "Lock types are not a subset of existing permissions.");

            return Lock<ECS, PermissionsSubset>(*this);
        }

        template<typename DynamicPermissions>
        std::optional<Lock<ECS, DynamicPermissions>> TryLock() const {
            using DynamicSorted = SortPermissions<DynamicPermissions, AllComponentTypes...>;
            if constexpr (has_permissions<DynamicPermissions>()) {
                return LockImpl<ECS, DynamicSorted>(instance, base, readAliasesWriteStorage);
            } else {
                static const auto requestedRead = ECS::template ReadBitset<DynamicSorted>();
                static const auto requestedWrite = ECS::template WriteBitset<DynamicSorted>();
                if ((requestedRead & base->readPermissions) == requestedRead &&
                    (requestedWrite & readAliasesWriteStorage) == requestedWrite) {
                    return LockImpl<ECS, DynamicSorted>(instance, base, readAliasesWriteStorage);
                }
                return {};
            }
        }

        long UseCount() const {
            return base.use_count();
        }

    private:
        template<size_t... Is>
        static constexpr ComponentBitset AcquireReadBitset(std::index_sequence<Is...>) {
            ComponentBitset result;
            result[0] = is_add_remove_optional<Permissions>();
            ((result[1 + Is] = is_read_optional<AllComponentTypes, Permissions>()), ...);
            return result | ECS::template ReadBitset<Permissions>();
        }

        static constexpr ComponentBitset AcquireReadBitset() {
            return AcquireReadBitset(std::make_index_sequence<sizeof...(AllComponentTypes)>());
        }

        void AcquireLockReference() {
            auto &t = *base;
            t.readLockReferences[0]++;
            if constexpr (is_add_remove_allowed<Permissions>()) {
                if (!t.writePermissions[0]) throw std::runtime_error("AddRemove lock not acquired");
                t.writeLockReferences[0]++;
            } else if constexpr (is_add_remove_optional<Permissions>()) {
                if (t.writePermissions[0]) t.writeLockReferences[0]++;
            }
            ( // For each AllComponentTypes
                [&] {
                    constexpr auto compIndex = 1 + ECS::template GetComponentIndex<AllComponentTypes>();
                    if constexpr (is_write_allowed<AllComponentTypes, Permissions>()) {
                        if (!t.writePermissions[compIndex]) throw std::runtime_error("Write lock not acquired");
                        t.writeLockReferences[compIndex]++;
                    } else if constexpr (is_write_optional<AllComponentTypes, Permissions>()) {
                        if (t.writePermissions[compIndex]) t.writeLockReferences[compIndex]++;
                    }
                    if constexpr (is_read_allowed<AllComponentTypes, Permissions>()) {
                        if (!t.readPermissions[compIndex]) throw std::runtime_error("Read lock not acquired");
                        t.readLockReferences[compIndex]++;
                    } else if constexpr (is_read_optional<AllComponentTypes, Permissions>()) {
                        if (t.readPermissions[compIndex]) t.readLockReferences[compIndex]++;
                    }
                }(),
                ...);
        }

        void ReleaseLockReference() {
            auto &t = *base;
            t.readLockReferences[0]--;
            if constexpr (is_add_remove_allowed<Permissions>()) {
                if (!t.writePermissions[0]) throw std::runtime_error("AddRemove lock not acquired");
                t.writeLockReferences[0]--;
            } else if constexpr (is_add_remove_optional<Permissions>()) {
                if (t.writePermissions[0]) t.writeLockReferences[0]--;
            }
            ( // For each AllComponentTypes
                [&] {
                    constexpr auto compIndex = 1 + ECS::template GetComponentIndex<AllComponentTypes>();
                    if constexpr (is_write_allowed<AllComponentTypes, Permissions>()) {
                        if (!t.writePermissions[compIndex]) throw std::runtime_error("Write lock not acquired");
                        t.writeLockReferences[compIndex]--;
                    } else if constexpr (is_write_optional<AllComponentTypes, Permissions>()) {
                        if (t.writePermissions[compIndex]) {
                            if (!t.writePermissions[compIndex]) throw std::runtime_error("Write lock not acquired");
                            t.writeLockReferences[compIndex]--;
                        }
                    }
                    if constexpr (is_read_allowed<AllComponentTypes, Permissions>()) {
                        if (!t.readPermissions[compIndex]) throw std::runtime_error("Read lock not acquired");
                        t.readLockReferences[compIndex]--;
                    } else if constexpr (is_read_optional<AllComponentTypes, Permissions>()) {
                        if (t.readPermissions[compIndex]) {
                            if (!t.readPermissions[compIndex]) throw std::runtime_error("Read lock not acquired");
                            t.readLockReferences[compIndex]--;
                        }
                    }

                    if (t.writeAccessedFlags[0] || t.writeAccessedFlags[compIndex]) return;
                    if (t.writePermissions[compIndex]) {
                        if (t.writeLockReferences[compIndex] == 0) {
                            if (t.readLockReferences[compIndex] == 0) {
                                t.writePermissions[compIndex] = false;
                                t.readPermissions[compIndex] = false;
                                instance.template Storage<AllComponentTypes>().WriteUnlock();
                            } else {
                                // t.writePermissions[compIndex] = false;
                                // instance.template Storage<AllComponentTypes>().WriteToReadLock();
                            }
                        }
                    } else if (t.readPermissions[compIndex]) {
                        if (t.readLockReferences[compIndex] == 0) {
                            t.readPermissions[compIndex] = false;
                            instance.template Storage<AllComponentTypes>().ReadUnlock();
                        }
                    }
                }(),
                ...);
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

        template<typename, typename...>
        friend class Lock;
        template<typename, typename>
        friend class LockImpl;
        friend struct Entity;
    };
}; // namespace Tecs
