#pragma once

#include "Tecs_entity.hh"
#include "Tecs_locks.hh"
#include "Tecs_template_util.hh"
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
            if (is_add_remove_allowed<LockType>()) {
                ecs.validIndex.StartWrite();
            } else {
                ecs.validIndex.RLock();
            }
            LockInOrder<AllComponentTypes...>();
        }

        inline ~Transaction() {
            UnlockInOrder<AllComponentTypes...>();
            if (is_add_remove_allowed<LockType>()) {
                // Rebuild writeValidEntities and validEntityIndexes with the new entity set.
                auto &newValidList = this->ecs.validIndex.writeValidEntities;
                auto &newValidIndexes = this->ecs.validIndex.validEntityIndexes;
                newValidList.clear();
                auto &bitsets = this->ecs.validIndex.writeComponents;
                for (size_t id = 0; id < bitsets.size(); id++) {
                    if (bitsets[id][0]) {
                        newValidIndexes[id] = newValidList.size();
                        newValidList.emplace_back(id);
                    }
                }
                this->ecs.validIndex.template CommitWrite<true>();
            } else {
                this->ecs.validIndex.RUnlock();
            }
        }

    private:
        // Call lock operations on LockedTypes in the same order they are defined in AllComponentTypes
        // This is accomplished by filtering AllComponentTypes by LockedTypes
        template<typename U>
        inline void LockInOrder() const {
            if (is_write_allowed<U, LockType>()) {
                this->ecs.template Storage<U>().StartWrite();
            } else if (is_read_allowed<U, LockType>()) {
                this->ecs.template Storage<U>().RLock();
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void LockInOrder() const {
            LockInOrder<U>();
            LockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void UnlockInOrder() const {
            if (is_write_allowed<U, LockType>()) {
                if (is_add_remove_allowed<LockType>()) {
                    // Rebuild writeValidEntities and validEntityIndexes with the new entity set.
                    auto &newValidList = this->ecs.template Storage<U>().writeValidEntities;
                    auto &newValidIndexes = this->ecs.template Storage<U>().validEntityIndexes;
                    newValidList.clear();
                    auto &bitsets = this->ecs.validIndex.writeComponents;
                    for (size_t id = 0; id < bitsets.size(); id++) {
                        if (this->ecs.template BitsetHas<U>(bitsets[id])) {
                            newValidIndexes[id] = newValidList.size();
                            newValidList.emplace_back(id);
                        }
                    }
                    this->ecs.template Storage<U>().template CommitWrite<true>();
                } else {
                    this->ecs.template Storage<U>().template CommitWrite<false>();
                }
            } else if (is_read_allowed<U, LockType>()) {
                this->ecs.template Storage<U>().RUnlock();
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
        inline Lock(Lock<ECS<AllComponentTypes...>, PermissionsSource...> &source)
            : permissions(source.permissions), ecs(source.base->ecs), base(source.base) {
            using SourceLockType = Lock<ECS<AllComponentTypes...>, PermissionsSource...>;
            static_assert(is_add_remove_allowed<SourceLockType>() || !is_add_remove_allowed<LockType>(),
                "AddRemove permission is missing.");
            static_assert(std::conjunction<is_lock_subset<AllComponentTypes, LockType, SourceLockType>...>(),
                "Lock types are not a subset of existing permissions.");
        }

        template<typename T>
        inline constexpr const nonstd::span<Entity> PreviousEntitiesWith() const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            return ecs.template Storage<T>().readValidEntities;
        }

        template<typename T>
        inline constexpr const nonstd::span<Entity> EntitiesWith() const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

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

        inline Entity NewEntity() {
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

        inline void DestroyEntity(Entity e) {
            static_assert(is_add_remove_allowed<LockType>(), "Lock does not have AddRemove permission.");

            // Invalidate the entity and all of its Components
            RemoveComponents<AllComponentTypes...>(e);
            ecs.validIndex.writeComponents[e.id][0] = false;
            size_t validIndex = ecs.validIndex.validEntityIndexes[e.id];
            ecs.validIndex.writeValidEntities[validIndex] = Entity();
            ecs.freeEntities.emplace_back(e);
        }

        template<typename... Tn>
        inline bool Has(const Entity &e) const {
            if (permissions[0]) {
                const auto &validBitset = ecs.validIndex.writeComponents[e.id];
                return ecs.template BitsetHas<Tn...>(validBitset);
            } else {
                const auto &validBitset = ecs.validIndex.readComponents[e.id];
                return ecs.template BitsetHas<Tn...>(validBitset);
            }
        }

        template<typename... Tn>
        inline bool Had(const Entity &e) const {
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
                    ecs.template Storage<T>().writeComponents[e.id] = {}; // Reset value before allowing reading.
                    validBitset[1 + ecs.template GetComponentIndex<T>()] = true;
                    auto &validEntities = ecs.template Storage<T>().writeValidEntities;
                    ecs.template Storage<T>().validEntityIndexes[e.id] = validEntities.size();
                    validEntities.emplace_back(e);
                } else {
                    throw std::runtime_error(
                        std::string("Entity does not have a component of type: ") + typeid(T).name());
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
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().readComponents[e.id];
        }

        template<typename T>
        inline T &Set(const Entity &e, T &value) {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");

            auto &validBitset =
                permissions[0] ? ecs.validIndex.writeComponents[e.id] : ecs.validIndex.readComponents[e.id];
            if (!ecs.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    validBitset[1 + ecs.template GetComponentIndex<T>()] = true;
                    auto &validEntities = ecs.template Storage<T>().writeValidEntities;
                    ecs.template Storage<T>().validEntityIndexes[e.id] = validEntities.size();
                    validEntities.emplace_back(e);
                } else {
                    throw std::runtime_error(
                        std::string("Entity does not have a component of type: ") + typeid(T).name());
                }
            }
            return ecs.template Storage<T>().writeComponents[e.id] = value;
        }

        template<typename T, typename... Args>
        inline T &Set(const Entity &e, Args... args) {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");

            auto &validBitset =
                permissions[0] ? ecs.validIndex.writeComponents[e.id] : ecs.validIndex.readComponents[e.id];
            if (!ecs.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    validBitset[1 + ecs.template GetComponentIndex<T>()] = true;
                    auto &validEntities = ecs.template Storage<T>().writeValidEntities;
                    ecs.template Storage<T>().validEntityIndexes[e.id] = validEntities.size();
                    validEntities.emplace_back(e);
                } else {
                    throw std::runtime_error(
                        std::string("Entity does not have a component of type: ") + typeid(T).name());
                }
            }
            return ecs.template Storage<T>().writeComponents[e.id] = T(args...);
        }

        template<typename... Tn>
        inline void Unset(const Entity &e) {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");

            RemoveComponents<Tn...>(e);
        }

        template<typename... PermissionsSubset>
        inline Lock<ECS<AllComponentTypes...>, PermissionsSubset...> Subset() {
            using NewLockType = Lock<ECS<AllComponentTypes...>, PermissionsSubset...>;
            static_assert(is_add_remove_allowed<LockType>() || !is_add_remove_allowed<NewLockType>(),
                "AddRemove permission is missing.");
            static_assert(std::conjunction<is_lock_subset<AllComponentTypes, NewLockType, LockType>...>(),
                "Lock types are not a subset of existing permissions.");

            return Lock<ECS<AllComponentTypes...>, PermissionsSubset...>(*this);
        }

    private:
        template<typename T>
        inline void AllocateComponents(size_t count) {
            size_t newSize = ecs.template Storage<T>().writeComponents.size() + count;
            ecs.template Storage<T>().writeComponents.resize(newSize);
            ecs.template Storage<T>().validEntityIndexes.resize(newSize);
        }

        template<typename T, typename T2, typename... Tn>
        inline void AllocateComponents(size_t count) {
            AllocateComponents<T>(count);
            AllocateComponents<T2, Tn...>(count);
        }

        template<typename T>
        inline void RemoveComponents(const Entity &e) {
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (ecs.template BitsetHas<T>(validBitset)) {
                validBitset[1 + ecs.template GetComponentIndex<T>()] = false;
                size_t validIndex = ecs.template Storage<T>().validEntityIndexes[e.id];
                ecs.template Storage<T>().writeValidEntities[validIndex] = Entity();
            }
        }

        template<typename T, typename T2, typename... Tn>
        inline void RemoveComponents(const Entity &e) {
            RemoveComponents<T>(e);
            RemoveComponents<T2, Tn...>(e);
        }

        ECS<AllComponentTypes...> &ecs;
        std::shared_ptr<BaseTransaction<ECS<AllComponentTypes...>>> base;

        template<typename, typename...>
        friend class Lock;
    };
}; // namespace Tecs
