#pragma once

#include "Tecs_entity.hh"
#include "Tecs_template_util.hh"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tecs {
    template<typename... Tn>
    class ECS;

    template<typename, typename...>
    class ReadLock {};
    template<typename, typename...>
    class ReadLockBase {};
    template<typename, typename...>
    class WriteLock {};
    template<typename, typename...>
    class WriteLockBase {};
    template<typename>
    class AddRemoveLock {};
    template<typename>
    class AddRemoveLockBase {};

    /**
     * ReadLock<Tn...> is a lock handle allowing read-only access to Component types specified in the template.
     *
     * Entities and Components cannot be added or removed while a ReadLock is held, and the values of Components
     * read through the ReadLock will remain constant until the ReadLock is freed by being deconstructed.
     */
    template<typename... AllComponentTypes, typename... LockedTypes>
    class ReadLock<ECS<AllComponentTypes...>, LockedTypes...> {
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
        inline operator ReadLock<ECS<AllComponentTypes...>, Tn...>() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLock<ECS<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline ReadLock<ECS<AllComponentTypes...>, Tn...> Subset() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLock<ECS<AllComponentTypes...>, Tn...>(ecs);
        }

    protected:
        ReadLock(ECS<AllComponentTypes...> &ecs) : ecs(ecs) {}

        ECS<AllComponentTypes...> &ecs;

        template<typename, typename...>
        friend class ReadLock;
        template<typename, typename...>
        friend class WriteLock;
        template<typename>
        friend class AddRemoveLock;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... LockedTypes>
    class ReadLockBase<ECSType<AllComponentTypes...>, LockedTypes...>
        : public ReadLock<ECSType<AllComponentTypes...>, LockedTypes...> {
    public:
        // Delete copy constructor
        ReadLockBase(const ReadLockBase<ECSType<AllComponentTypes...>, LockedTypes...> &) = delete;

        inline ReadLockBase(ECSType<AllComponentTypes...> &ecs)
            : ReadLock<ECSType<AllComponentTypes...>, LockedTypes...>(ecs) {
            ecs.validIndex.RLock();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~ReadLockBase() {
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
     * WriteLock<Tn...> is a lock handle allowing write access to Component types specified in the
     * template.
     *
     * Entities and Components cannot be added or removed while a WriteLock is in progress.
     * The values of valid Components may be modified through the WriteLock and will be applied when the WriteLock is
     * deconstructed. Each Component type can only be part of a single WriteLock at once.
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... LockedTypes>
    class WriteLock<ECSType<AllComponentTypes...>, LockedTypes...> {
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
        inline T &Set(const Entity &e, T &value) {
            static_assert(contains<T, LockedTypes...>::value, "Component type is not locked.");

            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return (ecs.template Storage<T>().writeComponents[e.id] = value);
        }

        template<typename T, typename... Args>
        inline T &Set(const Entity &e, Args... args) {
            static_assert(contains<T, LockedTypes...>::value, "Component type is not locked.");

            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return (ecs.template Storage<T>().writeComponents[e.id] = std::move(T(args...)));
        }

        // Reference as read lock of subset
        template<typename... Tn>
        inline operator ReadLock<ECSType<AllComponentTypes...>, Tn...>() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLock<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline ReadLock<ECSType<AllComponentTypes...>, Tn...> AsReadLock() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLock<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        // Reference as subset of lock
        template<typename... Tn>
        inline operator WriteLock<ECSType<AllComponentTypes...>, Tn...>() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return WriteLock<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline WriteLock<ECSType<AllComponentTypes...>, Tn...> Subset() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<LockedTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return WriteLock<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

    protected:
        WriteLock(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {}

        ECSType<AllComponentTypes...> &ecs;

        template<typename, typename...>
        friend class WriteLock;
        template<typename>
        friend class AddRemoveLock;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... LockedTypes>
    class WriteLockBase<ECSType<AllComponentTypes...>, LockedTypes...>
        : public WriteLock<ECSType<AllComponentTypes...>, LockedTypes...> {
    public:
        // Delete copy constructor
        WriteLockBase(const WriteLockBase<ECSType<AllComponentTypes...>, LockedTypes...> &) = delete;

        inline WriteLockBase(ECSType<AllComponentTypes...> &ecs)
            : WriteLock<ECSType<AllComponentTypes...>, LockedTypes...>(ecs) {
            ecs.validIndex.RLock();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~WriteLockBase() {
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
     * AddRemoveLock is a lock handle allowing creation and deletion of entities, as well as adding and removing of
     * Components to entities.
     *
     * An AddRemoveLock cannot be in progress at the same time as a WriteLock and will block until other transactions
     * have completed. In addition to allowing creation and deletion of entities, an AddRemoveLock also allows reads and
     * writes to all Component types as if they were part of a WriteLock or ReadLock.
     */
    template<template<typename...> typename ECSType, typename... AllComponentTypes>
    class AddRemoveLock<ECSType<AllComponentTypes...>> {
    public:
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
        inline T &Set(const Entity &e, T &value) {
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                validBitset[ecs.template GetComponentIndex<T>()] = true;
                ecs.template Storage<T>().writeValidEntities.emplace_back(e);
            }
            return (ecs.template Storage<T>().writeComponents[e.id] = value);
        }

        template<typename T, typename... Args>
        inline T &Set(const Entity &e, Args... args) {
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                validBitset[ecs.template GetComponentIndex<T>()] = true;
                ecs.template Storage<T>().writeValidEntities.emplace_back(e);
            }
            return (ecs.template Storage<T>().writeComponents[e.id] = std::move(T(args...)));
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
        inline operator ReadLock<ECSType<AllComponentTypes...>, Tn...>() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<AllComponentTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLock<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline ReadLock<ECSType<AllComponentTypes...>, Tn...> AsReadLock() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<AllComponentTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return ReadLock<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        // Reference as component write transaction
        template<typename... Tn>
        inline operator WriteLock<ECSType<AllComponentTypes...>, Tn...>() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<AllComponentTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return WriteLock<ECSType<AllComponentTypes...>, Tn...>(ecs);
        }

        template<typename... Tn>
        inline WriteLock<ECSType<AllComponentTypes...>, Tn...> AsWriteLock() {
            static_assert(is_subset_of<std::tuple<Tn...>, std::tuple<AllComponentTypes...>>::value,
                "Lock reference must be a subset of lock types.");
            return WriteLock<ECSType<AllComponentTypes...>, Tn...>(ecs);
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
        AddRemoveLock(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {}

        ECSType<AllComponentTypes...> &ecs;

        template<typename>
        friend class AddRemoveLock;
    };

    template<template<typename...> typename ECSType, typename... AllComponentTypes>
    class AddRemoveLockBase<ECSType<AllComponentTypes...>> : public AddRemoveLock<ECSType<AllComponentTypes...>> {
    public:
        // Delete copy constructor
        AddRemoveLockBase(const AddRemoveLockBase<ECSType<AllComponentTypes...>> &) = delete;

        inline AddRemoveLockBase(ECSType<AllComponentTypes...> &ecs)
            : AddRemoveLock<ECSType<AllComponentTypes...>>(ecs) {
            ecs.validIndex.StartWrite();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~AddRemoveLockBase() {
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
