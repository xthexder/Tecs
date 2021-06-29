#pragma once

#include "Tecs_entity.hh"
#include "Tecs_locks.hh"
#include "Tecs_observer.hh"
#include "nonstd/span.hpp"

#include <bitset>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#ifndef TECS_ENTITY_ALLOCATION_BATCH_SIZE
    #define TECS_ENTITY_ALLOCATION_BATCH_SIZE 1000
#endif

static_assert(TECS_ENTITY_ALLOCATION_BATCH_SIZE > 0, "At least 1 entity needs to be allocated at once.");

namespace Tecs {
    /**
     * When a Transaction is started, the relevant parts of the ECS are locked based on the Transactions Permissons.
     * The permissions can then be referenced by passing around Lock objects.
     *
     * Upon deconstruction, a Transaction will commit any changes written during its lifespan to the ECS.
     * Once a Transaction is deconstructed, all Locks referencing its permissions become invalid.
     */
    template<typename ECSType>
    class BaseTransaction {
    public:
        BaseTransaction(ECSType &ecs) : ecs(ecs) {}
        // Delete copy constructor
        BaseTransaction(const BaseTransaction &) = delete;

    protected:
        ECSType &ecs;

        template<typename, typename...>
        friend class Lock;
    };

    template<typename... AllComponentTypes, typename... Permissions>
    class Transaction<ECS<AllComponentTypes...>, Permissions...> : public BaseTransaction<ECS<AllComponentTypes...>> {
    private:
        using LockType = Lock<ECS<AllComponentTypes...>, Permissions...>;

    public:
        inline Transaction(ECS<AllComponentTypes...> &ecs) : BaseTransaction<ECS<AllComponentTypes...>>(ecs) {
            std::bitset<1 + sizeof...(AllComponentTypes)> acquired;
            // Templated lambda functions for Lock/Unlock so they can be looped over at runtime.
            std::array<std::function<bool(bool)>, acquired.size()> lockFuncs = {
                [&ecs](bool block) {
                    if (is_add_remove_allowed<LockType>()) {
                        return ecs.validIndex.WriteLock(block);
                    } else {
                        return ecs.validIndex.ReadLock(block);
                    }
                },
                [&ecs](bool block) {
                    if (is_write_allowed<AllComponentTypes, LockType>()) {
                        return ecs.template Storage<AllComponentTypes>().WriteLock(block);
                    } else if (is_read_allowed<AllComponentTypes, LockType>()) {
                        return ecs.template Storage<AllComponentTypes>().ReadLock(block);
                    }
                    // This component type isn't part of the lock, skip.
                    return true;
                }...};
            std::array<std::function<void()>, acquired.size()> unlockFuncs = {
                [&ecs]() {
                    if (is_add_remove_allowed<LockType>()) {
                        return ecs.validIndex.WriteUnlock();
                    } else {
                        return ecs.validIndex.ReadUnlock();
                    }
                },
                [&ecs]() {
                    if (is_write_allowed<AllComponentTypes, LockType>()) {
                        ecs.template Storage<AllComponentTypes>().WriteUnlock();
                    } else if (is_read_allowed<AllComponentTypes, LockType>()) {
                        ecs.template Storage<AllComponentTypes>().ReadUnlock();
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
        }

        inline ~Transaction() {
            if (is_add_remove_allowed<LockType>()) {
                // Rebuild writeValidEntities, validEntityIndexes, and freeEntities with the new entity set.
                this->ecs.validIndex.writeValidEntities.clear();
                ClearValidEntities<AllComponentTypes...>();
                this->ecs.freeEntities.clear();
                auto &bitsets = this->ecs.validIndex.writeComponents;
                for (size_t id = 0; id < bitsets.size(); id++) {
                    UpdateValidEntity<AllComponentTypes...>(id);
                    if (bitsets[id][0]) {
                        this->ecs.validIndex.validEntityIndexes[id] = this->ecs.validIndex.writeValidEntities.size();
                        this->ecs.validIndex.writeValidEntities.emplace_back(id);
                    } else {
                        this->ecs.freeEntities.emplace_back(Entity(id));
                    }
                }
            }
            CommitLockInOrder<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>()) {
                this->ecs.validIndex.CommitLock();
                auto &oldBitsets = this->ecs.validIndex.readComponents;
                auto &bitsets = this->ecs.validIndex.writeComponents;
                for (size_t id = 0; id < bitsets.size(); id++) {
                    NotifyObservers<AllComponentTypes...>(id);
                    if (bitsets[id][0]) {
                        if (id >= oldBitsets.size() || !oldBitsets[id][0]) {
                            for (auto &observer : this->ecs.template Observers<EntityAdded>()) {
                                observer->emplace_back(Entity(id));
                            }
                        }
                    } else if (id < oldBitsets.size() && oldBitsets[id][0]) {
                        for (auto &observer : this->ecs.template Observers<EntityRemoved>()) {
                            observer->emplace_back(Entity(id));
                        }
                    }
                }
            }
            UnlockInOrder<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>()) {
                this->ecs.validIndex.template CommitEntities<true>();
                this->ecs.validIndex.WriteUnlock();
            } else {
                this->ecs.validIndex.ReadUnlock();
            }
        }

    private:
        // Call lock operations on Permissions in the same order they are defined in AllComponentTypes
        // This is accomplished by filtering AllComponentTypes by Permissions
        template<typename U>
        inline void CommitLockInOrder() const {
            if (is_write_allowed<U, LockType>()) { this->ecs.template Storage<U>().CommitLock(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void CommitLockInOrder() const {
            CommitLockInOrder<U>();
            CommitLockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void ClearValidEntities() const {
            this->ecs.template Storage<U>().writeValidEntities.clear();
        }

        template<typename U, typename U2, typename... Un>
        inline void ClearValidEntities() const {
            ClearValidEntities<U>();
            ClearValidEntities<U2, Un...>();
        }

        template<typename U>
        inline void UpdateValidEntity(size_t id) const {
            auto &bitsets = this->ecs.validIndex.writeComponents;
            if (this->ecs.template BitsetHas<U>(bitsets[id])) {
                this->ecs.template Storage<U>().validEntityIndexes[id] =
                    this->ecs.template Storage<U>().writeValidEntities.size();
                this->ecs.template Storage<U>().writeValidEntities.emplace_back(id);
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void UpdateValidEntity(size_t id) const {
            UpdateValidEntity<U>(id);
            UpdateValidEntity<U2, Un...>(id);
        }

        template<typename U>
        inline void NotifyObservers(size_t id) const {
            auto &oldBitsets = this->ecs.validIndex.readComponents;
            auto &bitsets = this->ecs.validIndex.writeComponents;
            if (this->ecs.template BitsetHas<U>(bitsets[id])) {
                if (id >= oldBitsets.size() || !this->ecs.template BitsetHas<U>(oldBitsets[id])) {
                    for (auto &observer : this->ecs.template Observers<Added<U>>()) {
                        observer->emplace_back(Entity(id), this->ecs.template Storage<U>().writeComponents[id]);
                    }
                }
            } else if (id < oldBitsets.size() && this->ecs.template BitsetHas<U>(oldBitsets[id])) {
                for (auto &observer : this->ecs.template Observers<Removed<U>>()) {
                    observer->emplace_back(Entity(id), this->ecs.template Storage<U>().readComponents[id]);
                }
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void NotifyObservers(size_t id) const {
            NotifyObservers<U>(id);
            NotifyObservers<U2, Un...>(id);
        }

        template<typename U>
        inline void UnlockInOrder() const {
            if (is_write_allowed<U, LockType>()) {
                if (is_add_remove_allowed<LockType>()) {
                    this->ecs.template Storage<U>().template CommitEntities<true>();
                } else {
                    this->ecs.template Storage<U>().template CommitEntities<false>();
                }
                this->ecs.template Storage<U>().WriteUnlock();
            } else if (is_read_allowed<U, LockType>()) {
                this->ecs.template Storage<U>().ReadUnlock();
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

        std::bitset<1 + sizeof...(AllComponentTypes)> permissions;

        template<typename T>
        inline void SetPermissionBit() {
            permissions[1 + ECS<AllComponentTypes...>::template GetComponentIndex<T>()] =
                is_write_allowed<T, LockType>();
        }

    public:
        // Start a new transaction
        inline Lock(ECS<AllComponentTypes...> &ecs)
            : ecs(ecs), base(new Transaction<ECS<AllComponentTypes...>, Permissions...>(ecs)) {
            permissions[0] = is_add_remove_allowed<LockType>();
            (SetPermissionBit<AllComponentTypes>(), ...);
        }

        // Reference an existing transaction
        template<typename... PermissionsSource>
        inline Lock(const Lock<ECS<AllComponentTypes...>, PermissionsSource...> &source)
            : permissions(source.permissions), ecs(source.base->ecs), base(source.base) {
            using SourceLockType = Lock<ECS<AllComponentTypes...>, PermissionsSource...>;
            static_assert(is_add_remove_allowed<SourceLockType>() || !is_add_remove_allowed<LockType>(),
                "AddRemove permission is missing.");
            static_assert(std::conjunction<is_lock_subset<AllComponentTypes, LockType, SourceLockType>...>(),
                "Lock types are not a subset of existing permissions.");
        }

        template<typename T>
        inline constexpr const nonstd::span<Entity> PreviousEntitiesWith() const {
            return ecs.template Storage<T>().readValidEntities;
        }

        template<typename T>
        inline constexpr const nonstd::span<Entity> EntitiesWith() const {
            if (permissions[0]) {
                return ecs.template Storage<T>().writeValidEntities;
            } else {
                return ecs.template Storage<T>().readValidEntities;
            }
        }

        inline constexpr const nonstd::span<Entity> PreviousEntities() const {
            return ecs.validIndex.readValidEntities;
        }

        inline constexpr const nonstd::span<Entity> Entities() const {
            if (permissions[0]) {
                return ecs.validIndex.writeValidEntities;
            } else {
                return ecs.validIndex.readValidEntities;
            }
        }

        inline Entity NewEntity() const {
            static_assert(is_add_remove_allowed<LockType>(), "Lock does not have AddRemove permission.");

            Entity entity;
            if (ecs.freeEntities.empty()) {
                // Allocate a new set of entities and components
                AllocateComponents<AllComponentTypes...>(TECS_ENTITY_ALLOCATION_BATCH_SIZE);
                entity.id = ecs.validIndex.writeComponents.size();
                size_t newSize = entity.id + TECS_ENTITY_ALLOCATION_BATCH_SIZE;
                ecs.validIndex.writeComponents.resize(newSize);
                ecs.validIndex.validEntityIndexes.resize(newSize);

                // Add all but 1 of the new Entity ids to the free list.
                for (size_t id = 1; id < TECS_ENTITY_ALLOCATION_BATCH_SIZE; id++) {
                    ecs.freeEntities.emplace_back(entity.id + id);
                }
            } else {
                entity = ecs.freeEntities.front();
                ecs.freeEntities.pop_front();
            }

            ecs.validIndex.writeComponents[entity.id][0] = true;
            auto &validEntities = ecs.validIndex.writeValidEntities;
            ecs.validIndex.validEntityIndexes[entity.id] = validEntities.size();
            validEntities.emplace_back(entity);

            return entity;
        }

        inline void DestroyEntity(Entity e) const {
            static_assert(is_add_remove_allowed<LockType>(), "Lock does not have AddRemove permission.");

            // Invalidate the entity and all of its Components
            RemoveComponents<AllComponentTypes...>(e);
            ecs.validIndex.writeComponents[e.id][0] = false;
            size_t validIndex = ecs.validIndex.validEntityIndexes[e.id];
            ecs.validIndex.writeValidEntities[validIndex] = Entity();
        }

        inline bool Exists(const Entity &e) const {
            if (permissions[0]) {
                if (e.id >= ecs.validIndex.writeComponents.size()) return false;
                const auto &validBitset = ecs.validIndex.writeComponents[e.id];
                return validBitset[0];
            } else {
                if (e.id >= ecs.validIndex.readComponents.size()) return false;
                const auto &validBitset = ecs.validIndex.readComponents[e.id];
                return validBitset[0];
            }
        }

        template<typename... Tn>
        inline bool Has(const Entity &e) const {
            if (permissions[0]) {
                if (e.id >= ecs.validIndex.writeComponents.size()) return false;
                const auto &validBitset = ecs.validIndex.writeComponents[e.id];
                return ecs.template BitsetHas<Tn...>(validBitset);
            } else {
                if (e.id >= ecs.validIndex.readComponents.size()) return false;
                const auto &validBitset = ecs.validIndex.readComponents[e.id];
                return ecs.template BitsetHas<Tn...>(validBitset);
            }
        }

        template<typename... Tn>
        inline bool Had(const Entity &e) const {
            if (e.id >= ecs.validIndex.readComponents.size()) return false;
            const auto &validBitset = ecs.validIndex.readComponents[e.id];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename T, typename ReturnType = std::conditional_t<is_write_allowed<T, LockType>::value, T, const T>>
        inline ReturnType &Get(const Entity &e) const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");
            static_assert(is_write_allowed<T, LockType>() || std::is_const<ReturnType>(),
                "Can't get non-const reference of read only Component.");

            auto &validBitset =
                permissions[0] ? ecs.validIndex.writeComponents[e.id] : ecs.validIndex.readComponents[e.id];
            if (!ecs.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    if (validBitset[0]) {
                        ecs.template Storage<T>().writeComponents[e.id] = {}; // Reset value before allowing reading.
                        validBitset[1 + ecs.template GetComponentIndex<T>()] = true;
                        auto &validEntities = ecs.template Storage<T>().writeValidEntities;
                        ecs.template Storage<T>().validEntityIndexes[e.id] = validEntities.size();
                        validEntities.emplace_back(e);
                    } else {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(e.id));
                    }
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            if (permissions[1 + ecs.template GetComponentIndex<T>()]) {
                return ecs.template Storage<T>().writeComponents[e.id];
            } else {
                return ecs.template Storage<T>().readComponents[e.id];
            }
        }

        template<typename T>
        inline const T &GetPrevious(const Entity &e) const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            const auto &validBitset = ecs.validIndex.readComponents[e.id];
            if (!ecs.template BitsetHas<T>(validBitset)) {
                throw std::runtime_error("Entity does not have a component of type: " + std::string(typeid(T).name()));
            }
            return ecs.template Storage<T>().readComponents[e.id];
        }

        template<typename T>
        inline T &Set(const Entity &e, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");

            auto &validBitset =
                permissions[0] ? ecs.validIndex.writeComponents[e.id] : ecs.validIndex.readComponents[e.id];
            if (!ecs.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    if (validBitset[0]) {
                        validBitset[1 + ecs.template GetComponentIndex<T>()] = true;
                        auto &validEntities = ecs.template Storage<T>().writeValidEntities;
                        ecs.template Storage<T>().validEntityIndexes[e.id] = validEntities.size();
                        validEntities.emplace_back(e);
                    } else {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(e.id));
                    }
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            return ecs.template Storage<T>().writeComponents[e.id] = value;
        }

        template<typename T, typename... Args>
        inline T &Set(const Entity &e, Args &&...args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");

            auto &validBitset =
                permissions[0] ? ecs.validIndex.writeComponents[e.id] : ecs.validIndex.readComponents[e.id];
            if (!ecs.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    if (validBitset[0]) {
                        validBitset[1 + ecs.template GetComponentIndex<T>()] = true;
                        auto &validEntities = ecs.template Storage<T>().writeValidEntities;
                        ecs.template Storage<T>().validEntityIndexes[e.id] = validEntities.size();
                        validEntities.emplace_back(e);
                    } else {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(e.id));
                    }
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            return ecs.template Storage<T>().writeComponents[e.id] = T(std::forward<Args>(args)...);
        }

        template<typename... Tn>
        inline void Unset(const Entity &e) const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");

            RemoveComponents<Tn...>(e);
        }

        template<typename Event>
        inline Observer<ECS<AllComponentTypes...>, Event> Watch() const {
            static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to watch for ecs changes.");

            auto &eventList = ecs.template Observers<Event>().emplace_back(std::make_shared<std::deque<Event>>());
            return Observer(ecs, eventList);
        }

        template<typename Event>
        inline void StopWatching(Observer<ECS<AllComponentTypes...>, Event> &observer) const {
            static_assert(is_add_remove_allowed<LockType>(), "An AddRemove lock is required to stop an observer.");
            auto eventList = observer.eventListWeak.lock();
            auto &observers = ecs.template Observers<Event>();
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
            size_t newSize = ecs.template Storage<T>().writeComponents.size() + count;
            ecs.template Storage<T>().writeComponents.resize(newSize);
            ecs.template Storage<T>().validEntityIndexes.resize(newSize);
        }

        template<typename T, typename T2, typename... Tn>
        inline void AllocateComponents(size_t count) const {
            AllocateComponents<T>(count);
            AllocateComponents<T2, Tn...>(count);
        }

        template<typename T>
        inline void RemoveComponents(const Entity &e) const {
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (ecs.template BitsetHas<T>(validBitset)) {
                validBitset[1 + ecs.template GetComponentIndex<T>()] = false;
                size_t validIndex = ecs.template Storage<T>().validEntityIndexes[e.id];
                ecs.template Storage<T>().writeValidEntities[validIndex] = Entity();
            }
        }

        template<typename T, typename T2, typename... Tn>
        inline void RemoveComponents(const Entity &e) const {
            RemoveComponents<T>(e);
            RemoveComponents<T2, Tn...>(e);
        }

        ECS<AllComponentTypes...> &ecs;
        std::shared_ptr<BaseTransaction<ECS<AllComponentTypes...>>> base;

        template<typename, typename...>
        friend class Lock;
    };
}; // namespace Tecs
