#pragma once

#include "Tecs_entity.hh"
#include "Tecs_locks.hh"
#include "Tecs_template_util.hh"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace Tecs {
    /**
     * Lock<ECS, Permissions...> is a reference to lock permissions held by an active Transaction.
     *
     * Permissions... can be any combination of the following:
     * Tecs::Read<Components...>
     * Tecs::Write<Components...>
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

    public:
        template<typename T>
        inline constexpr const std::vector<Entity> &PreviousEntitiesWith() const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            return ecs.template Storage<T>().readValidEntities;
        }

        template<typename T>
        inline constexpr const std::vector<Entity> &EntitiesWith() const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            if (is_write_allowed<T, LockType>()) {
                return ecs.template Storage<T>().writeValidEntities;
            } else {
                return ecs.template Storage<T>().readValidEntities;
            }
        }

        inline Entity AddEntity() {
            static_assert(is_add_remove_allowed<LockType>(), "Lock does not have AddRemove permission.");

            AddEntityToComponents<AllComponentTypes...>();
            Entity id(ecs.validIndex.writeComponents.size());
            ecs.validIndex.writeComponents.emplace_back();
            return id;
        }

        template<typename... Tn>
        inline bool Has(const Entity &e) const {
            if (is_add_remove_allowed<LockType>()) {
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

            auto &validBitset = is_add_remove_allowed<LockType>() ? ecs.validIndex.writeComponents[e.id]
                                                                  : ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                if (is_add_remove_allowed<LockType>()) {
                    ecs.template Storage<T>().writeComponents[e.id] = {}; // Reset value before allowing reading.
                    validBitset[ecs.template GetComponentIndex<T>()] = true;
                    ecs.template Storage<T>().writeValidEntities.emplace_back(e);
                } else {
                    throw std::runtime_error(
                        std::string("Entity does not have a component of type: ") + typeid(T).name());
                }
            }
            if (is_write_allowed<T, LockType>()) {
                return ecs.template Storage<T>().writeComponents[e.id];
            } else {
                return ecs.template Storage<T>().readComponents[e.id];
            }
        }

        template<typename T>
        inline const T &GetPrevious(const Entity &e) const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            const auto &validBitset = ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().readComponents[e.id];
        }

        template<typename T>
        inline T &Set(const Entity &e, T &value) {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");

            auto &validBitset = is_add_remove_allowed<LockType>() ? ecs.validIndex.writeComponents[e.id]
                                                                  : ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                if (is_add_remove_allowed<LockType>()) {
                    validBitset[ecs.template GetComponentIndex<T>()] = true;
                    ecs.template Storage<T>().writeValidEntities.emplace_back(e);
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

            auto &validBitset = is_add_remove_allowed<LockType>() ? ecs.validIndex.writeComponents[e.id]
                                                                  : ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                if (is_add_remove_allowed<LockType>()) {
                    validBitset[ecs.template GetComponentIndex<T>()] = true;
                    ecs.template Storage<T>().writeValidEntities.emplace_back(e);
                } else {
                    throw std::runtime_error(
                        std::string("Entity does not have a component of type: ") + typeid(T).name());
                }
            }
            return ecs.template Storage<T>().writeComponents[e.id] = std::move(T(args...));
        }

        template<typename T>
        inline void Unset(const Entity &e) {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");

            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (validBitset[ecs.template GetComponentIndex<T>()]) {
                validBitset[ecs.template GetComponentIndex<T>()] = false;
                auto &validEntities = ecs.template Storage<T>().writeValidEntities;
                for (auto itr = validEntities.begin(); itr != validEntities.end(); itr++) {
                    if (*itr == e) {
                        validEntities.erase(itr);
                        break;
                    }
                }
            }
        }

        template<typename... PermissionsSubset>
        inline Lock<ECS<AllComponentTypes...>, PermissionsSubset...> Subset() {
            using NewLockType = Lock<ECS<AllComponentTypes...>, PermissionsSubset...>;
            static_assert(!is_add_remove_allowed<NewLockType>() ||
                              (is_add_remove_allowed<NewLockType>() && !is_add_remove_allowed<LockType>()),
                "AddRemove permission is missing.");
            static_assert(std::conjunction<is_lock_subset<AllComponentTypes, LockType, NewLockType>...>(),
                "Lock types are not a subset of existing permissions.");

            return Lock<ECS<AllComponentTypes...>, PermissionsSubset...>(ecs);
        }

        template<typename... PermissionsSubset>
        inline operator Lock<ECS<AllComponentTypes...>, PermissionsSubset...>() {
            return Subset<PermissionsSubset...>();
        }

    protected:
        Lock(ECS<AllComponentTypes...> &ecs) : ecs(ecs) {}

        ECS<AllComponentTypes...> &ecs;

    private:
        template<typename U>
        inline void AddEntityToComponents() {
            ecs.template Storage<U>().writeComponents.emplace_back();
        }

        template<typename U, typename U2, typename... Un>
        inline void AddEntityToComponents() {
            AddEntityToComponents<U>();
            AddEntityToComponents<U2, Un...>();
        }

        template<typename, typename...>
        friend class Lock;
    };

    /**
     * When a Transaction is started, the relevant parts of the ECS are locked based on the Transactions Permissons.
     * The permissions can then be referenced by passing around Lock objects.
     *
     * Upon deconstruction, a Transaction will commit any changes written during its lifespan to the ECS.
     * Once a Transaction is deconstructed, all Locks referencing its permissions become invalid.
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... Permissions>
    class Transaction<ECSType<AllComponentTypes...>, Permissions...>
        : public Lock<ECSType<AllComponentTypes...>, Permissions...> {
    private:
        using LockType = Lock<ECS<AllComponentTypes...>, Permissions...>;

    public:
        // Delete copy constructor
        Transaction(const Transaction<ECSType<AllComponentTypes...>, Permissions...> &) = delete;

        inline Transaction(ECSType<AllComponentTypes...> &ecs)
            : Lock<ECSType<AllComponentTypes...>, Permissions...>(ecs) {
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
}; // namespace Tecs
