#pragma once

#include "Tecs_entity.hh"
#include "Tecs_template_util.hh"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace Tecs {
    template<typename... Tn>
    class ECS;

    /**
     * Lock permissions are passed in as template arguments when creating a Transaction or Lock.
     *
     * Examples:
     * using ECSType = Tecs::ECS<A, B, C>;
     *
     * // Allow read access to A and B components, and write access to C components.
     * Transaction<ECSType, Read<A, B>, Write<C>> transaction1 = ecs.NewTransaction<Read<A, B>, Write<C>>();
     *
     * // Allow read and write access to all components, as well as adding and removing entities and components.
     * Transaction<ECSType, AddRemove> transaction2 = ecs.NewTransaction<AddRemove>();
     *
     * // Reference a transaction's permissions (or a subset of them) by using the Lock type.
     * Lock<ECSType, AddRemove> lockAll = transaction2;
     * Lock<ECSType, Read<A>, Write<B>> lockWriteB = transaction1;
     *
     * // Locks can be automatically cast to subsets of existing locks.
     * Lock<ECSType, Read<A, B>> lockReadAB = lockWriteB;
     * Lock<ECSType, Read<B>> lockReadB = lockReadAB;
     */
    template<typename... LockedTypes>
    struct Read {};
    template<typename... LockedTypes>
    struct Write {};
    struct AddRemove {};

    template<typename, typename...>
    class Lock {};
    template<typename, typename...>
    class Transaction {};

    /*
     * Compile time helpers for determining lock permissions.
     */
    template<typename T, typename Lock>
    struct is_read_allowed : std::false_type {};
    template<typename T, typename Lock>
    struct is_write_allowed : std::false_type {};
    template<typename Lock>
    struct is_add_remove_allowed : std::false_type {};
    template<typename T, typename Lock, typename SubLock>
    struct is_lock_subset : std::false_type {};

    // Lock<Permissions...> and Transaction<Permissions...> specializations
    template<typename T, template<typename, typename...> typename L, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, L<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, template<typename, typename...> typename L, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, L<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};
    template<template<typename, typename...> typename L, typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<L<ECSType, Permissions...>> : contains<AddRemove, Permissions...> {};

    // Welcome to template hell
    template<typename T, template<typename, typename...> typename L, typename ECSType, typename... Permissions,
        typename... PermissionsSub>
    struct is_lock_subset<T, L<ECSType, Permissions...>, L<ECSType, PermissionsSub...>>
        : std::conjunction<std::disjunction<std::conditional_t<is_write_allowed<T, PermissionsSub>::value,
                               is_write_allowed<T, Permissions>, std::true_type>>...,
              std::disjunction<std::conditional_t<is_read_allowed<T, PermissionsSub>::value,
                  is_read_allowed<T, Permissions>, std::true_type>>...> {};

    // Read<LockedTypes...> specialization
    template<typename T, typename... LockedTypes>
    struct is_read_allowed<T, Read<LockedTypes...>> : contains<T, LockedTypes...> {};

    // Write<LockedTypes...> specialization
    template<typename T, typename... LockedTypes>
    struct is_read_allowed<T, Write<LockedTypes...>> : contains<T, LockedTypes...> {};
    template<typename T, typename... LockedTypes>
    struct is_write_allowed<T, Write<LockedTypes...>> : contains<T, LockedTypes...> {};

    // AddRemove specialization
    template<typename T>
    struct is_read_allowed<T, AddRemove> : std::true_type {};
    template<typename T>
    struct is_write_allowed<T, AddRemove> : std::true_type {};
    template<>
    struct is_add_remove_allowed<AddRemove> : std::true_type {};

    /**
     * ReadLock<Tn...> is a lock handle allowing read-only access to Component types specified in the template.
     *
     * Entities and Components cannot be added or removed while a ReadLock is held, and the values of Components
     * read through the ReadLock will remain constant until the ReadLock is freed by being deconstructed.
     */
    template<typename... AllComponentTypes, typename... Permissions>
    class Lock<ECS<AllComponentTypes...>, Permissions...> {
    private:
        using LockType = Lock<ECS<AllComponentTypes...>, Permissions...>;

    public:
        template<typename T>
        inline constexpr const std::vector<Entity> &PreviousValidEntities() const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            return ecs.template Storage<T>().readValidEntities;
        }

        template<typename T>
        inline constexpr const std::vector<Entity> &ValidEntities() const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            if (is_write_allowed<T, LockType>()) {
                return ecs.template Storage<T>().writeValidEntities;
            } else {
                return ecs.template Storage<T>().readValidEntities;
            }
        }

        inline Entity AddEntity() {
            AddEntityToComponents<AllComponentTypes...>();
            Entity id(ecs.validIndex.writeComponents.size());
            ecs.validIndex.writeComponents.emplace_back();
            return id;
        }

        template<typename... Tn>
        inline bool Had(const Entity &e) const {
            const auto &validBitset = ecs.validIndex.readComponents[e.id];
            return ecs.template BitsetHas<Tn...>(validBitset);
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

        template<typename T>
        inline const T &GetPrevious(const Entity &e) const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            const auto &validBitset = ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().readComponents[e.id];
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

    // TODO: Rewrite me for Transaction
    /**
     * WriteLock<Tn...> is a lock handle allowing write access to Component types specified in the
     * template.
     *
     * Entities and Components cannot be added or removed while a WriteLock is in progress.
     * The values of valid Components may be modified through the WriteLock and will be applied when the WriteLock is
     * deconstructed. Each Component type can only be part of a single WriteLock at once.
     */

    /**
     * AddRemoveLock is a lock handle allowing creation and deletion of entities, as well as adding and removing of
     * Components to entities.
     *
     * An AddRemoveLock cannot be in progress at the same time as a WriteLock and will block until other transactions
     * have completed. In addition to allowing creation and deletion of entities, an AddRemoveLock also allows reads and
     * writes to all Component types as if they were part of a WriteLock or ReadLock.
     */

}; // namespace Tecs
