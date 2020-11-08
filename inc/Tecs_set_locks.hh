#pragma once

#include "Tecs_entity.hh"
#include "Tecs_template_util.hh"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tecs {
    template<typename, typename...>
    class ReadLockRef {};
    template<typename, typename...>
    class ReadLock {};
    template<typename, typename...>
    class ComponentWriteTransactionRef {};
    template<typename, typename...>
    class ComponentWriteTransaction {};
    template<typename>
    class EntityWriteTransactionRef {};
    template<typename>
    class EntityWriteTransaction {};

    /**
     * ReadLock<Tn...> is a lock handle allowing read-only access to Component types specified in the template.
     *
     * Entities and Components cannot be added or removed while a ReadLock is held, and the values of Components
     * read through the ReadLock will remain constant until the ReadLock is freed by being deconstructed.
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... LockedTypes>
    class ReadLockRef<ECSType<AllComponentTypes...>, LockedTypes...> {
    public:
        template<typename T>
        inline constexpr const std::vector<Entity> &ValidEntities() const {
            return ecs.template Storage<T>().readValidEntities;
        }

        template<typename... Tn>
        inline bool Has(const Entity &e) const {
            auto &validBitset = ecs.validIndex.readComponents[e.id];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename T>
        inline const T &Get(const Entity &e) const {
            static_assert(contains<T, LockedTypes...>::value, "Component type is not locked.");

            auto &validBitset = ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().readComponents[e.id];
        }

        template<typename... Tn>
        inline operator ReadLockRef<ECSType<AllComponentTypes...>, Tn...>() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLockRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline ReadLockRef<ECSType<AllComponentTypes...>, Tn...> Subset() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLockRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

    protected:
        ReadLockRef(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {}

        ECSType<AllComponentTypes...> &ecs;

        template<typename, typename...>
        friend class ReadLockRef;
        template<typename, typename...>
        friend class ComponentWriteTransactionRef;
        template<typename>
        friend class EntityWriteTransactionRef;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... LockedTypes>
    class ReadLock<ECSType<AllComponentTypes...>, LockedTypes...>
        : public ReadLockRef<ECSType<AllComponentTypes...>, LockedTypes...> {
    public:
        // Delete copy constructor
        ReadLock(const ReadLock<ECSType<AllComponentTypes...>, LockedTypes...> &) = delete;

        inline ReadLock(ECSType<AllComponentTypes...> &ecs)
            : ReadLockRef<ECSType<AllComponentTypes...>, LockedTypes...>(ecs) {
            ecs.validIndex.RLock();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~ReadLock() {
            UnlockInOrder<AllComponentTypes...>();
            this->ecs.validIndex.RUnlock();
        }

    private:
        // Call lock operations on LockedTypes in the same order they are defined in AllComponentTypes
        // This is accomplished by filtering AllComponentTypes by LockedTypes
        template<typename U>
        inline void LockInOrder() {
            if (contains<U, LockedTypes...>::value) { this->ecs.template Storage<U>().RLock(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void LockInOrder() {
            LockInOrder<U>();
            LockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void UnlockInOrder() {
            if (contains<U, LockedTypes...>::value) { this->ecs.template Storage<U>().RUnlock(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void UnlockInOrder() {
            UnlockInOrder<U2, Un...>();
            UnlockInOrder<U>();
        }
    };

    /**
     * ComponentWriteTransaction<Tn...> is a lock handle allowing write access to Component types specified in the
     * template.
     *
     * Entities and Components cannot be added or removed while a ComponentWriteTransaction is in progress.
     * The values of valid Components may be modified through the ComponentWriteTransaction and will be
     * applied when the ComponentWriteTransaction is deconstructed.
     * Each Component type can only be part of a single ComponentWriteTransaction at once.
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... LockedTypes>
    class ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, LockedTypes...> {
    public:
        template<typename T>
        inline constexpr const std::vector<Entity> &PreviousValidEntities() const {
            return ecs.template Storage<T>().readValidEntities;
        }

        template<typename T>
        inline constexpr const std::vector<Entity> &ValidEntities() const {
            return ecs.template Storage<T>().writeValidEntities;
        }

        template<typename... Tn>
        inline bool Had(const Entity &e) const {
            auto &validBitset = ecs.validIndex.readComponents[e.id];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename... Tn>
        inline bool Has(const Entity &e) const {
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename T>
        inline const T &GetPrevious(const Entity &e) const {
            static_assert(contains<T, LockedTypes...>::value, "Component type is not locked.");

            auto &validBitset = ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().readComponents[e.id];
        }

        template<typename T>
        inline T &Get(const Entity &e) {
            static_assert(contains<T, LockedTypes...>::value, "Component type is not locked.");

            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().writeComponents[e.id];
        }

        template<typename T>
        inline void Set(const Entity &e, T &value) {
            static_assert(contains<T, LockedTypes...>::value, "Component type is not locked.");

            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            ecs.template Storage<T>().writeComponents[e.id] = value;
        }

        template<typename T, typename... Args>
        inline void Set(const Entity &e, Args... args) {
            static_assert(contains<T, LockedTypes...>::value, "Component type is not locked.");

            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            ecs.template Storage<T>().writeComponents[e.id] = std::move(T(args...));
        }

        // Reference as read lock of subset
        template<typename... Tn>
        inline operator ReadLockRef<ECSType<AllComponentTypes...>, Tn...>() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLockRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline ReadLockRef<ECSType<AllComponentTypes...>, Tn...> ReadLock() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLockRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        // Reference as subset of lock
        template<typename... Tn>
        inline operator ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, Tn...>() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, Tn...> Subset() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

    protected:
        ComponentWriteTransactionRef(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {}

        ECSType<AllComponentTypes...> &ecs;

        template<typename, typename...>
        friend class ComponentWriteTransactionRef;
        template<typename>
        friend class EntityWriteTransactionRef;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... LockedTypes>
    class ComponentWriteTransaction<ECSType<AllComponentTypes...>, LockedTypes...>
        : public ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, LockedTypes...> {
    public:
        // Delete copy constructor
        ComponentWriteTransaction(
            const ComponentWriteTransaction<ECSType<AllComponentTypes...>, LockedTypes...> &) = delete;

        inline ComponentWriteTransaction(ECSType<AllComponentTypes...> &ecs)
            : ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, LockedTypes...>(ecs) {
            ecs.validIndex.RLock();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~ComponentWriteTransaction() {
            UnlockInOrder<AllComponentTypes...>();
            this->ecs.validIndex.RUnlock();
        }

    private:
        // Call lock operations on LockedTypes in the same order they are defined in AllComponentTypes
        // This is accomplished by filtering AllComponentTypes by LockedTypes
        template<typename U>
        inline void LockInOrder() {
            if (contains<U, LockedTypes...>::value) { this->ecs.template Storage<U>().StartWrite(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void LockInOrder() {
            LockInOrder<U>();
            LockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void UnlockInOrder() {
            if (contains<U, LockedTypes...>::value) { this->ecs.template Storage<U>().template CommitWrite<false>(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void UnlockInOrder() {
            UnlockInOrder<U2, Un...>();
            UnlockInOrder<U>();
        }
    };

    /**
     * EntityWriteTransaction<Tn...> is a lock handle allowing creation and deletion of entities, as well as
     * adding and removing of Components to entities.
     *
     * An EntityWriteTransaction cannot be in progress at the same time as a ComponentWriteTransaction and will block
     * until other transactions have completed.
     * In addition to allowing creation and deletion of entities, a EntityWriteTransaction also allows write to all
     * Component types as if they were part of a ComponentWriteTransaction.
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes>
    class EntityWriteTransactionRef<ECSType<AllComponentTypes...>> {
    public:
        EntityWriteTransactionRef(const EntityWriteTransactionRef<ECSType<AllComponentTypes...>> &readLock)
            : ecs(readLock.ecs) {}

        template<typename T>
        inline constexpr const std::vector<Entity> &PreviousValidEntities() const {
            return ecs.template Storage<T>().readValidEntities;
        }

        template<typename T>
        inline constexpr const std::vector<Entity> &ValidEntities() const {
            return ecs.template Storage<T>().writeValidEntities;
        }

        inline Entity AddEntity() {
            AddEntityToComponents<AllComponentTypes...>();
            Entity id(ecs.validIndex.writeComponents.size());
            ecs.validIndex.writeComponents.emplace_back();
            return id;
        }

        template<typename... Tn>
        inline bool Had(const Entity &e) const {
            auto &validBitset = ecs.validIndex.readComponents[e.id];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename... Tn>
        inline bool Has(const Entity &e) const {
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename T>
        inline const T &GetPrevious(const Entity &e) const {
            auto &validBitset = ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().readComponents[e.id];
        }

        template<typename T>
        inline T &Get(const Entity &e) {
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().writeComponents[e.id];
        }

        template<typename T>
        inline void Set(const Entity &e, T &value) {
            ecs.template Storage<T>().writeComponents[e.id] = value;
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                validBitset[ecs.template GetComponentIndex<T>()] = true;
                ecs.template Storage<T>().writeValidEntities.emplace_back(e);
            }
        }

        template<typename T, typename... Args>
        inline void Set(const Entity &e, Args... args) {
            ecs.template Storage<T>().writeComponents[e.id] = std::move(T(args...));
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                validBitset[ecs.template GetComponentIndex<T>()] = true;
                ecs.template Storage<T>().writeValidEntities.emplace_back(e);
            }
        }

        template<typename T>
        inline void Unset(const Entity &e) {
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

        // Reference as read lock of subset
        template<typename... Tn>
        inline operator ReadLockRef<ECSType<AllComponentTypes...>, Tn...>() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<AllComponentTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLockRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline ReadLockRef<ECSType<AllComponentTypes...>, Tn...> ReadLock() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<AllComponentTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLockRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        // Reference as component write transaction
        template<typename... Tn>
        inline operator ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, Tn...>() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<AllComponentTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, Tn...> ComponentWriteTransaction() const {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<AllComponentTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ComponentWriteTransactionRef<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

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

    protected:
        EntityWriteTransactionRef(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {}

        ECSType<AllComponentTypes...> &ecs;

        template<typename>
        friend class EntityWriteTransactionRef;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes>
    class EntityWriteTransaction<ECSType<AllComponentTypes...>>
        : public EntityWriteTransactionRef<ECSType<AllComponentTypes...>> {
    public:
        // Delete copy constructor
        EntityWriteTransaction(const EntityWriteTransaction<ECSType<AllComponentTypes...>> &) = delete;

        inline EntityWriteTransaction(ECSType<AllComponentTypes...> &ecs)
            : EntityWriteTransactionRef<ECSType<AllComponentTypes...>>(ecs) {
            ecs.validIndex.StartWrite();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~EntityWriteTransaction() {
            UnlockInOrder<AllComponentTypes...>();
            this->ecs.validIndex.template CommitWrite<true>();
        }

    private:
        // Call lock operations in the order they are defined in AllComponentTypes
        template<typename U>
        inline void LockInOrder() {
            this->ecs.template Storage<U>().StartWrite();
        }

        template<typename U, typename U2, typename... Un>
        inline void LockInOrder() {
            LockInOrder<U>();
            LockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void UnlockInOrder() {
            this->ecs.template Storage<U>().template CommitWrite<true>();
        }

        template<typename U, typename U2, typename... Un>
        inline void UnlockInOrder() {
            UnlockInOrder<U2, Un...>();
            UnlockInOrder<U>();
        }
    };
}; // namespace Tecs
