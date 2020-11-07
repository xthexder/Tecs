#pragma once

#include "Tecs_entity.hh"
#include "Tecs_template_util.hh"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tecs {
    /**
     * ReadLock<Tn...> is a lock handle allowing read-only access to Component types specified in the template.
     *
     * Entities and Components cannot be added or removed while a ReadLock is held, and the values of Components
     * read through the ReadLock will remain constant until the ReadLock is freed by being deconstructed.
     */
    template<typename, typename...>
    class ReadLock {};

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... ReadComponentTypes>
    class ReadLock<ECSType<AllComponentTypes...>, ReadComponentTypes...> {
    public:
        inline ReadLock(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {
            ecs.validIndex.RLock();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~ReadLock() {
            UnlockInOrder<AllComponentTypes...>();
            ecs.validIndex.RUnlock();
        }

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
            auto &validBitset = ecs.validIndex.readComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().readComponents[e.id];
        }

    private:
        // Call lock operations on ReadComponentTypes in the same order they are defined in AllComponentTypes
        // This is accomplished by filtering AllComponentTypes by ReadComponentTypes
        template<typename U>
        inline void LockInOrder() {
            if (is_type_in_set<U, ReadComponentTypes...>::value) { ecs.template Storage<U>().RLock(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void LockInOrder() {
            LockInOrder<U>();
            LockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void UnlockInOrder() {
            if (is_type_in_set<U, ReadComponentTypes...>::value) { ecs.template Storage<U>().RUnlock(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void UnlockInOrder() {
            UnlockInOrder<U2, Un...>();
            UnlockInOrder<U>();
        }

        ECSType<AllComponentTypes...> &ecs;
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
    template<typename, typename...>
    class ComponentWriteTransaction {};

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... WriteComponentTypes>
    class ComponentWriteTransaction<ECSType<AllComponentTypes...>, WriteComponentTypes...> {
    public:
        inline ComponentWriteTransaction(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {
            ecs.validIndex.RLock();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~ComponentWriteTransaction() {
            UnlockInOrder<AllComponentTypes...>();
            ecs.validIndex.RUnlock();
        }

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
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            ecs.template Storage<T>().writeComponents[e.id] = value;
        }

        template<typename T, typename... Args>
        inline void Set(const Entity &e, Args... args) {
            auto &validBitset = ecs.validIndex.writeComponents[e.id];
            if (!validBitset[ecs.template GetComponentIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            ecs.template Storage<T>().writeComponents[e.id] = std::move(T(args...));
        }

    private:
        // Call lock operations on WriteComponentTypes in the same order they are defined in AllComponentTypes
        // This is accomplished by filtering AllComponentTypes by WriteComponentTypes
        template<typename U>
        inline void LockInOrder() {
            if (is_type_in_set<U, WriteComponentTypes...>::value) { ecs.template Storage<U>().StartWrite(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void LockInOrder() {
            LockInOrder<U>();
            LockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void UnlockInOrder() {
            if (is_type_in_set<U, WriteComponentTypes...>::value) {
                ecs.template Storage<U>().template CommitWrite<false>();
            }
        }

        template<typename U, typename U2, typename... Un>
        inline void UnlockInOrder() {
            UnlockInOrder<U2, Un...>();
            UnlockInOrder<U>();
        }

        ECSType<AllComponentTypes...> &ecs;
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
    template<typename>
    class EntityWriteTransaction {};

    template<template<typename...> typename ECSType, typename... AllComponentTypes>
    class EntityWriteTransaction<ECSType<AllComponentTypes...>> {
    public:
        inline EntityWriteTransaction(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {
            ecs.validIndex.StartWrite();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~EntityWriteTransaction() {
            UnlockInOrder<AllComponentTypes...>();
            ecs.validIndex.template CommitWrite<true>();
        }

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

    private:
        // Call lock operations in the order they are defined in AllComponentTypes
        template<typename U>
        inline void LockInOrder() {
            ecs.template Storage<U>().StartWrite();
        }

        template<typename U, typename U2, typename... Un>
        inline void LockInOrder() {
            LockInOrder<U>();
            LockInOrder<U2, Un...>();
        }

        template<typename U>
        inline void UnlockInOrder() {
            ecs.template Storage<U>().template CommitWrite<true>();
        }

        template<typename U, typename U2, typename... Un>
        inline void UnlockInOrder() {
            UnlockInOrder<U2, Un...>();
            UnlockInOrder<U>();
        }

        template<typename U>
        inline void AddEntityToComponents() {
            ecs.template Storage<U>().writeComponents.emplace_back();
        }

        template<typename U, typename U2, typename... Un>
        inline void AddEntityToComponents() {
            AddEntityToComponents<U>();
            AddEntityToComponents<U2, Un...>();
        }

        ECSType<AllComponentTypes...> &ecs;
    };
}; // namespace Tecs
