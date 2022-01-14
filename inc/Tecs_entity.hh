#pragma once

#include "Tecs_permissions.hh"

#include <cstddef>
#include <functional>
#include <stdexcept>

#ifndef TECS_ENTITY_ID_TYPE
    #define TECS_ENTITY_ID_TYPE uint64_t
#endif
#ifndef TECS_ENTITY_GENERATION_TYPE
    // Default to half the entity id bits being used for index, and half for generation count.
    #define TECS_ENTITY_GENERATION_TYPE uint32_t
#endif
static_assert(sizeof(TECS_ENTITY_GENERATION_TYPE) < sizeof(TECS_ENTITY_ID_TYPE), "TECS_ENTITY_GENERATION_TYPE must be smaller than TECS_ENTITY_ID_TYPE");

namespace Tecs {
    struct EntityId {
        TECS_ENTITY_ID_TYPE value = 0;
        
        static const size_t IndexBits = (sizeof(TECS_ENTITY_ID_TYPE) - sizeof(TECS_ENTITY_GENERATION_TYPE)) * 8;

        EntityId() {}
        explicit EntityId(TECS_ENTITY_ID_TYPE index, TECS_ENTITY_GENERATION_TYPE gen = 1) {
            value = index;
            if (Generation() != 0) {
                throw std::runtime_error("Entity index overflows into generation id");
            }
            value |= (TECS_ENTITY_ID_TYPE)gen << IndexBits;
        }

        TECS_ENTITY_GENERATION_TYPE Generation() const {
            return value >> IndexBits;
        }

        size_t Index() const {
            return value & (((size_t)1 << IndexBits) - 1);
        }

        explicit operator size_t() const {
            return Index();
        }

        inline bool operator==(const EntityId &other) const {
            return value == other.value;
        }

        inline bool operator!=(const EntityId &other) const {
            return value != other.value;
        }

        inline bool operator<(const EntityId &other) const {
            return Index() < other.Index();
        }

        inline bool operator!() const {
            return Generation() == 0;
        }

        inline explicit operator bool() const {
            return Generation() != 0;
        }
    };
}

namespace std {
    inline string to_string(const Tecs::EntityId &id) {
        return "Entity::Id(" + to_string(id.Generation()) + ", " + to_string(id.Index()) + ")";
    }
} // namespace std


namespace Tecs {
    struct Entity {
        EntityId id;

        inline Entity() : id() {}
        inline Entity(EntityId id) : id(id) {}

        template<typename LockType>
        inline bool Existed(LockType &lock) const {
            if (id >= lock.instance.validIndex.readComponents.size()) return false;
            const auto &validBitset = lock.instance.validIndex.readComponents[id];
            return validBitset[0];
        }

        template<typename LockType>
        inline bool Exists(LockType &lock) const {
            if (lock.permissions[0]) {
                if (id >= lock.instance.validIndex.writeComponents.size()) return false;
                const auto &validBitset = lock.instance.validIndex.writeComponents[id];
                return validBitset[0];
            } else {
                if (id >= lock.instance.validIndex.readComponents.size()) return false;
                const auto &validBitset = lock.instance.validIndex.readComponents[id];
                return validBitset[0];
            }
        }

        template<typename... Tn, typename LockType>
        inline bool Has(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            if (lock.permissions[0]) {
                if (id >= lock.instance.validIndex.writeComponents.size()) return false;
                const auto &validBitset = lock.instance.validIndex.writeComponents[id];
                return validBitset[0] && lock.instance.template BitsetHas<Tn...>(validBitset);
            } else {
                if (id >= lock.instance.validIndex.readComponents.size()) return false;
                const auto &validBitset = lock.instance.validIndex.readComponents[id];
                return validBitset[0] && lock.instance.template BitsetHas<Tn...>(validBitset);
            }
        }

        template<typename... Tn, typename LockType>
        inline bool Had(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            if (id >= lock.instance.validIndex.readComponents.size()) return false;
            const auto &validBitset = lock.instance.validIndex.readComponents[id];
            return validBitset[0] && lock.instance.template BitsetHas<Tn...>(validBitset);
        }

        template<typename T, typename LockType,
            typename ReturnType =
                std::conditional_t<is_write_allowed<std::remove_cv_t<T>, LockType>::value, T, const T>>
        inline ReturnType &Get(LockType &lock) const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(is_write_allowed<CompType, LockType>() || std::is_const<ReturnType>(),
                "Can't get non-const reference of read only Component.");
            static_assert(!is_global_component<CompType>(), "Global components must be accessed through lock.Get()");

            if (!std::is_const<ReturnType>()) {
                lock.base->writeAccessedFlags[1 + lock.instance.template GetComponentIndex<CompType>()] = true;
            }

            if (lock.permissions[0]) {
                if (id >= lock.instance.validIndex.writeComponents.size()) {
                    throw std::runtime_error("Entity is invalid");
                }
            } else if (id >= lock.instance.validIndex.readComponents.size()) {
                throw std::runtime_error("Entity is invalid");
            }

            auto &validBitset = lock.permissions[0] ? lock.instance.validIndex.writeComponents[id]
                                                    : lock.instance.validIndex.readComponents[id];
            if (!validBitset[0] || !lock.instance.template BitsetHas<CompType>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    if (validBitset[0]) {
                        lock.base->writeAccessedFlags[0] = true;

                        // Reset value before allowing reading.
                        lock.instance.template Storage<CompType>().writeComponents[id] = {};
                        validBitset[1 + lock.instance.template GetComponentIndex<CompType>()] = true;
                        auto &validEntities = lock.instance.template Storage<CompType>().writeValidEntities;
                        lock.instance.template Storage<CompType>().validEntityIndexes[id] = validEntities.size();
                        validEntities.emplace_back(*this);
                    } else {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(id));
                    }
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
                }
            }
            if (lock.permissions[1 + lock.instance.template GetComponentIndex<CompType>()]) {
                return lock.instance.template Storage<CompType>().writeComponents[id];
            } else {
                return lock.instance.template Storage<CompType>().readComponents[id];
            }
        }

        template<typename T, typename LockType>
        inline const T &GetPrevious(LockType &lock) const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(!is_global_component<CompType>(),
                "Global components must be accessed through lock.GetPrevious()");

            if (id >= lock.instance.validIndex.readComponents.size()) throw std::runtime_error("Entity is invalid");

            const auto &validBitset = lock.instance.validIndex.readComponents[id];
            if (!validBitset[0] || !lock.instance.template BitsetHas<CompType>(validBitset)) {
                throw std::runtime_error(
                    "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
            }
            return lock.instance.template Storage<CompType>().readComponents[id];
        }

        template<typename T, typename LockType>
        inline T &Set(LockType &lock, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.base->writeAccessedFlags[1 + lock.instance.template GetComponentIndex<T>()] = true;

            if (id >= lock.instance.validIndex.writeComponents.size()) {
                throw std::runtime_error("Entity is invalid");
            }

            auto &validBitset = lock.instance.validIndex.writeComponents[id];
            if (!validBitset[0] || !lock.instance.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    if (validBitset[0]) {
                        lock.base->writeAccessedFlags[0] = true;

                        validBitset[1 + lock.instance.template GetComponentIndex<T>()] = true;
                        auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                        lock.instance.template Storage<T>().validEntityIndexes[id] = validEntities.size();
                        validEntities.emplace_back(*this);
                    } else {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(id));
                    }
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            return lock.instance.template Storage<T>().writeComponents[id] = value;
        }

        template<typename T, typename LockType, typename... Args>
        inline T &Set(LockType &lock, Args... args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.base->writeAccessedFlags[1 + lock.instance.template GetComponentIndex<T>()] = true;

            if (id >= lock.instance.validIndex.writeComponents.size()) {
                throw std::runtime_error("Entity is invalid");
            }

            auto &validBitset = lock.instance.validIndex.writeComponents[id];
            if (!validBitset[0] || !lock.instance.template BitsetHas<T>(validBitset)) {
                if (is_add_remove_allowed<LockType>()) {
                    if (validBitset[0]) {
                        lock.base->writeAccessedFlags[0] = true;

                        validBitset[1 + lock.instance.template GetComponentIndex<T>()] = true;
                        auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                        lock.instance.template Storage<T>().validEntityIndexes[id] = validEntities.size();
                        validEntities.emplace_back(*this);
                    } else {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(id));
                    }
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            return lock.instance.template Storage<T>().writeComponents[id] = T(std::forward<Args>(args)...);
        }

        template<typename... Tn, typename LockType>
        inline void Unset(LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");
            static_assert(!contains_global_components<Tn...>(),
                "Global components must be removed through lock.Unset()");

            if (id >= lock.instance.validIndex.writeComponents.size()) {
                throw std::runtime_error("Entity is invalid");
            }

            (lock.template RemoveComponents<Tn>(*this), ...);
        }

        template<typename LockType>
        inline void Destroy(LockType &lock) {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be destroyed without an AddRemove lock.");
            lock.base->writeAccessedFlags[0] = true;

            if (id >= lock.instance.validIndex.writeComponents.size()) {
                throw std::runtime_error("Entity is invalid");
            }

            Tecs::Entity copy = *this;
            id = 0;

            // Invalidate the entity and all of its Components
            lock.instance.validIndex.writeComponents[copy.id][0] = false;
            size_t validIndex = lock.instance.validIndex.validEntityIndexes[copy.id];
            lock.instance.validIndex.writeValidEntities[validIndex] = Entity();
            lock.RemoveAllComponents(copy);
        }

        inline bool operator==(const Entity &other) const {
            return id == other.id;
        }

        inline bool operator!=(const Entity &other) const {
            return id != other.id;
        }

        inline bool operator<(const Entity &other) const {
            return id < other.id;
        }

        inline bool operator!() const {
            return !id;
        }

        inline explicit operator bool() const {
            return (bool)id;
        }
    };
} // namespace Tecs

namespace std {
    template<>
    struct hash<Tecs::Entity> {
        std::size_t operator()(const Tecs::Entity &e) const {
            return e.id.value;
        }
    };
} // namespace std
