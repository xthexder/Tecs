#pragma once

#include "Tecs_permissions.hh"

#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>

#ifndef TECS_ENTITY_ID_TYPE
    #define TECS_ENTITY_ID_TYPE uint64_t
#endif

namespace Tecs {
    struct Entity {
        TECS_ENTITY_ID_TYPE id;

        inline Entity() : id(std::numeric_limits<TECS_ENTITY_ID_TYPE>::max()) {}
        inline Entity(TECS_ENTITY_ID_TYPE id) : id(id) {}

    public:
        template<typename LockType>
        inline bool Exists(LockType &lock) const {
            if (lock.permissions[0]) {
                if (id >= lock.instance.metadata.writeComponents.size()) return false;
                return lock.instance.metadata.writeComponents[id][0];
            } else {
                if (id >= lock.instance.metadata.readComponents.size()) return false;
                return lock.instance.metadata.readComponents[id][0];
            }
        }

        template<typename LockType>
        inline bool Existed(LockType &lock) const {
            if (id >= lock.instance.metadata.readComponents.size()) return false;
            return lock.instance.metadata.readComponents[id][0];
        }

        template<typename... Tn, typename LockType>
        inline bool Has(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            auto &metadataList =
                lock.permissions[0] ? lock.instance.metadata.writeComponents : lock.instance.metadata.readComponents;
            if (id >= metadataList.size()) return false;
            auto &metadata = metadataList[id];
            return metadata[0] && lock.instance.template BitsetHas<Tn...>(metadata);
        }

        template<typename... Tn, typename LockType>
        inline bool Had(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            if (id >= lock.instance.metadata.readComponents.size()) return false;
            auto &metadata = lock.instance.metadata.readComponents[id];
            return metadata[0] && lock.instance.template BitsetHas<Tn...>(metadata);
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

            if constexpr (!std::is_const<ReturnType>()) lock.base->template SetAccessFlag<CompType>(true);

            auto &metadataList =
                lock.permissions[0] ? lock.instance.metadata.writeComponents : lock.instance.metadata.readComponents;
            if (id >= metadataList.size()) throw std::runtime_error("Entity does not exist: InvalidId");
            auto &metadata = metadataList[id];
            if (!metadata[0]) throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            if (!lock.instance.template BitsetHas<CompType>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    lock.base->writeAccessedFlags[0] = true;

                    // Reset value before allowing reading.
                    lock.instance.template Storage<CompType>().writeComponents[id] = {};
                    metadata[1 + lock.instance.template GetComponentIndex<CompType>()] = true;
                    auto &validEntities = lock.instance.template Storage<CompType>().writeValidEntities;
                    lock.instance.template Storage<CompType>().validEntityIndexes[id] = validEntities.size();
                    validEntities.emplace_back(*this);
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
                }
            }

            if (lock.instance.template BitsetHas<CompType>(lock.permissions)) {
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

            if (id >= lock.instance.metadata.readComponents.size()) {
                throw std::runtime_error("Entity does not exist: InvalidId");
            }
            auto &metadata = lock.instance.metadata.readComponents[id];
            if (!metadata[0]) throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            if (!lock.instance.template BitsetHas<CompType>(metadata)) {
                throw std::runtime_error(
                    "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
            }
            return lock.instance.template Storage<CompType>().readComponents[id];
        }

        template<typename T, typename LockType>
        inline T &Set(LockType &lock, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.base->template SetAccessFlag<T>(true);

            if (id >= lock.instance.metadata.writeComponents.size()) {
                throw std::runtime_error("Entity does not exist: InvalidId");
            }

            auto &metadata = lock.instance.metadata.writeComponents[id];
            if (!metadata[0]) throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            if (!lock.instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    lock.base->writeAccessedFlags[0] = true;

                    metadata[1 + lock.instance.template GetComponentIndex<T>()] = true;
                    auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                    lock.instance.template Storage<T>().validEntityIndexes[id] = validEntities.size();
                    validEntities.emplace_back(*this);
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
            lock.base->template SetAccessFlag<T>(true);

            if (id >= lock.instance.metadata.writeComponents.size()) {
                throw std::runtime_error("Entity does not exist: InvalidId");
            }

            auto &metadata = lock.instance.metadata.writeComponents[id];
            if (!metadata[0]) throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            if (!lock.instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    lock.base->writeAccessedFlags[0] = true;

                    metadata[1 + lock.instance.template GetComponentIndex<T>()] = true;
                    auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                    lock.instance.template Storage<T>().validEntityIndexes[id] = validEntities.size();
                    validEntities.emplace_back(*this);
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

            if (id >= lock.instance.metadata.writeComponents.size()) {
                throw std::runtime_error("Entity does not exist: InvalidId");
            }
            if (!lock.instance.metadata.writeComponents[id][0]) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            (lock.template RemoveComponents<Tn>(id), ...);
        }

        template<typename LockType>
        inline void Destroy(LockType &lock) {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be destroyed without an AddRemove lock.");
            lock.base->writeAccessedFlags[0] = true;

            if (id >= lock.instance.metadata.writeComponents.size()) {
                throw std::runtime_error("Entity does not exist: InvalidId");
            }
            if (!lock.instance.metadata.writeComponents[id][0]) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            // It is possible for *this to be a reference to a component's writeValidEntities list
            // Copy the index before removing components so we can complete the removal.
            size_t index = id;

            // Invalidate the entity and all of its Components
            lock.RemoveAllComponents(index);
            lock.instance.metadata.writeComponents[index] = lock.instance.EmptyMetadataRef();
            size_t validIndex = lock.instance.metadata.validEntityIndexes[index];
            lock.instance.metadata.writeValidEntities[validIndex] = Entity();
            id = std::numeric_limits<TECS_ENTITY_ID_TYPE>::max();
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
            return id == std::numeric_limits<TECS_ENTITY_ID_TYPE>::max();
        }

        inline explicit operator bool() const {
            return id != std::numeric_limits<TECS_ENTITY_ID_TYPE>::max();
        }
    };
} // namespace Tecs

namespace std {
    template<>
    struct hash<Tecs::Entity> {
        std::size_t operator()(const Tecs::Entity &e) const {
            return e.id;
        }
    };
} // namespace std
