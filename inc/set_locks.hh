#pragma once

#include "template_util.hh"

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
        inline constexpr const std::vector<size_t> &ValidIndexes() const {
            return ecs.template Storage<T>().readValidIndexes;
        }

        template<typename... Tn>
        inline bool Has(size_t entityId) const {
            auto &validBitset = ecs.validIndex.readComponents[entityId];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename T>
        inline const T &Get(size_t entityId) const {
            auto &validBitset = ecs.validIndex.readComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().readComponents[entityId];
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
        inline constexpr const std::vector<size_t> &ValidIndexes() const {
            return ecs.template Storage<T>().writeValidIndexes;
        }

        template<typename... Tn>
        inline bool Has(size_t entityId) const {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename T>
        inline const T &Get(size_t entityId) const {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().writeComponents[entityId];
        }

        template<typename T>
        inline T &Get(size_t entityId) {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().writeComponents[entityId];
        }

        template<typename T>
        inline void Set(size_t entityId, T &value) {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            ecs.template Storage<T>().writeComponents[entityId] = value;
        }

        template<typename T, typename... Args>
        inline void Set(size_t entityId, Args... args) {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            ecs.template Storage<T>().writeComponents[entityId] = std::move(T(args...));
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
        inline constexpr const std::vector<size_t> &ValidIndexes() const {
            return ecs.template Storage<T>().writeValidIndexes;
        }

        inline size_t AddEntity() {
            AddEntityToComponents<AllComponentTypes...>();
            size_t id = ecs.validIndex.writeComponents.size();
            ecs.validIndex.writeComponents.emplace_back();
            return id;
        }

        template<typename... Tn>
        inline bool Has(size_t entityId) const {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            return ecs.template BitsetHas<Tn...>(validBitset);
        }

        template<typename T>
        inline const T &Get(size_t entityId) const {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().writeComponents[entityId];
        }

        template<typename T>
        inline T &Get(size_t entityId) {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                throw std::runtime_error(std::string("Entity does not have a component of type: ") + typeid(T).name());
            }
            return ecs.template Storage<T>().writeComponents[entityId];
        }

        template<typename T>
        inline void Set(size_t entityId, T &value) {
            ecs.template Storage<T>().writeComponents[entityId] = value;
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                validBitset[ecs.template GetIndex<T>()] = true;
                ecs.template Storage<T>().writeValidIndexes.emplace_back(entityId);
            }
        }

        template<typename T, typename... Args>
        inline void Set(size_t entityId, Args... args) {
            ecs.template Storage<T>().writeComponents[entityId] = std::move(T(args...));
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                validBitset[ecs.template GetIndex<T>()] = true;
                ecs.template Storage<T>().writeValidIndexes.emplace_back(entityId);
            }
        }

        template<typename T>
        inline void Unset(size_t entityId) {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (validBitset[ecs.template GetIndex<T>()]) {
                validBitset[ecs.template GetIndex<T>()] = false;
                auto &validIndexes = ecs.template Storage<T>().writeValidIndexes;
                for (auto itr = validIndexes.begin(); itr != validIndexes.end(); itr++) {
                    if (*itr == entityId) {
                        validIndexes.erase(itr);
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
