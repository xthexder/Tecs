#pragma once

#include <cstddef>
#include <vector>

namespace Tecs {

    template<typename, typename...>
    class ComponentSetReadLock {};

    template<template<typename...> typename ECSType, typename... AllComponentTypes, typename... ReadComponentTypes>
    class ComponentSetReadLock<ECSType<AllComponentTypes...>, ReadComponentTypes...> {
    public:
        inline ComponentSetReadLock(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {
            ecs.validIndex.RLock();
            LockInOrder<AllComponentTypes...>();
        }

        inline ~ComponentSetReadLock() {
            UnlockInOrder<AllComponentTypes...>();
            ecs.validIndex.RUnlock();
        }

        template<typename T>
        inline constexpr const std::vector<size_t> &ValidIndexes() const {
            return ecs.template Storage<T>().readValidIndexes;
        }

        template<typename T>
        inline bool Has(size_t entityId) const {
            auto &validBitset = ecs.validIndex.readComponents[entityId];
            return ecs.template BitsetHas<T>(validBitset);
        }

        template<typename T, typename T2, typename... Tn>
        inline bool Has(size_t entityId) const {
            auto &validBitset = ecs.validIndex.readComponents[entityId];
            return ecs.template BitsetHas<T>(validBitset) && ecs.template BitsetHas<T2, Tn...>(validBitset);
        }

        template<typename T>
        inline const T &Get(size_t entityId) const {
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

    template<typename, bool, typename...>
    class ComponentSetWriteTransaction {};

    template<template<typename...> typename ECSType, typename... AllComponentTypes, bool AllowAddRemove,
        typename... WriteComponentTypes>
    class ComponentSetWriteTransaction<ECSType<AllComponentTypes...>, AllowAddRemove, WriteComponentTypes...> {
    public:
        inline ComponentSetWriteTransaction(ECSType<AllComponentTypes...> &ecs) : ecs(ecs) {
            if (AllowAddRemove) {
                ecs.validIndex.StartWrite();
            } else {
                ecs.validIndex.RLock();
            }
            LockInOrder<AllComponentTypes...>();
        }

        inline ~ComponentSetWriteTransaction() {
            UnlockInOrder<AllComponentTypes...>();
            if (AllowAddRemove) {
                ecs.validIndex.CommitWrite();
            } else {
                ecs.validIndex.RUnlock();
            }
        }

        template<typename T>
        inline constexpr const std::vector<size_t> &ValidIndexes() const {
            return ecs.template Storage<T>().writeValidIndexes;
        }

        inline size_t AddEntity() {
            if (AllowAddRemove) {
                AddEntityToComponents<AllComponentTypes...>();
                size_t id = ecs.validIndex.writeComponents.size();
                ecs.validIndex.writeComponents.emplace_back();
                ecs.validIndex.writeValidDirty = true;
                return id;
            } else {
                // throw std::runtime_error("Can't Add entity without setting AllowAddRemove to true");
                while (1)
                    ;
                ;
            }
        }

        template<typename T>
        inline bool Has(size_t entityId) const {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            return ecs.template BitsetHas<T>(validBitset);
        }

        template<typename T, typename T2, typename... Tn>
        inline bool Has(size_t entityId) const {
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            return ecs.template BitsetHas<T>(validBitset) && ecs.template BitsetHas<T2, Tn...>(validBitset);
        }

        template<typename T>
        inline const T &Get(size_t entityId) const {
            return ecs.template Storage<T>().writeComponents[entityId];
        }

        template<typename T>
        inline T &Get(size_t entityId) {
            return ecs.template Storage<T>().writeComponents[entityId];
        }

        template<typename T>
        inline void Set(size_t entityId, T &value) {
            ecs.template Storage<T>().writeComponents[entityId] = value;
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                if (!AllowAddRemove) {
                    // throw std::runtime_error("Can't add new component without setting AllowAddRemove to true");
                    while (1)
                        ;
                    ;
                }
                validBitset[ecs.template GetIndex<T>()] = true;
                ecs.template Storage<T>().writeValidIndexes.emplace_back(entityId);
                ecs.template Storage<T>().writeValidDirty = true;
            }
        }

        template<typename T, typename... Args>
        inline void Set(size_t entityId, Args... args) {
            ecs.template Storage<T>().writeComponents[entityId] = std::move(T(args...));
            auto &validBitset = ecs.validIndex.writeComponents[entityId];
            if (!validBitset[ecs.template GetIndex<T>()]) {
                if (!AllowAddRemove) {
                    // throw std::runtime_error("Can't add new component without setting AllowAddRemove to true");
                    while (1)
                        ;
                    ;
                }
                validBitset[ecs.template GetIndex<T>()] = true;
                ecs.template Storage<T>().writeValidIndexes.emplace_back(entityId);
                ecs.template Storage<T>().writeValidDirty = true;
            }
        }

        template<typename T>
        inline void Unset(size_t entityId) {
            if (AllowAddRemove) {
                auto &validBitset = ecs.validIndex.writeComponents[entityId];
                if (validBitset[ecs.template GetIndex<T>()]) {
                    validBitset[ecs.template GetIndex<T>()] = false;
                    // TODO: Find index in ecs.template Storage<T>().writeValidIndexes and remove it.
                    ecs.template Storage<T>().writeValidDirty = true;
                }
            } else {
                // throw std::runtime_error("Can't remove entity without setting AllowAddRemove to true");
                while (1)
                    ;
                ;
            }
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
            if (is_type_in_set<U, WriteComponentTypes...>::value) { ecs.template Storage<U>().CommitWrite(); }
        }

        template<typename U, typename U2, typename... Un>
        inline void UnlockInOrder() {
            UnlockInOrder<U2, Un...>();
            UnlockInOrder<U>();
        }

        template<typename U>
        inline void AddEntityToComponents() {
            ecs.template Storage<U>().writeComponents.emplace_back();
            ecs.template Storage<U>().writeValidDirty = true;
        }

        template<typename U, typename U2, typename... Un>
        inline void AddEntityToComponents() {
            AddEntityToComponents<U>();
            AddEntityToComponents<U2, Un...>();
        }

        ECSType<AllComponentTypes...> &ecs;
    };
}; // namespace Tecs
