#pragma once

#include "Tecs_permissions.hh"

#include <cstddef>
#include <functional>
#include <stdexcept>

#ifndef TECS_ENTITY_ID_TYPE
    #define TECS_ENTITY_ID_TYPE uint64_t
#endif

namespace Tecs {
    struct EntityId {
        TECS_ENTITY_ID_TYPE index = 0;

        EntityId() {}
        EntityId(TECS_ENTITY_ID_TYPE index) : index(index) {}

        inline bool operator==(const EntityId &other) const {
            return index == other.index;
        }

        inline bool operator!=(const EntityId &other) const {
            return !(*this == other);
        }

        inline bool operator<(const EntityId &other) const {
            return index < other.index;
        }

        inline bool operator!() const {
            return index == std::numeric_limits<TECS_ENTITY_ID_TYPE>::max();
        }

        inline explicit operator bool() const {
            return index != std::numeric_limits<TECS_ENTITY_ID_TYPE>::max();
        }
    };
    static_assert(sizeof(EntityId) == sizeof(uint64_t), "EntityId is too large!");
} // namespace Tecs

namespace std {
    inline string to_string(const Tecs::EntityId &id) {
        return "Id(" + to_string(id.index) + ")";
    }
} // namespace std

namespace Tecs {
    struct Entity {
        EntityId id;

        inline Entity() : id() {}
        inline Entity(EntityId id) : id(id) {}

    public:
        template<typename LockType>
        inline bool Exists(LockType &lock) const {
            if (lock.permissions.HasGlobal()) {
                return lock.WriteMetadata(id).template Has<>();
            } else {
                return lock.ReadMetadata(id).template Has<>();
            }
        }

        template<typename LockType>
        inline bool Existed(LockType &lock) const {
            return lock.ReadMetadata(id).template Has<>();
        }

        template<typename... Tn, typename LockType>
        inline bool Has(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            if (lock.permissions.HasGlobal()) {
                return lock.WriteMetadata(id).template Has<Tn...>();
            } else {
                return lock.ReadMetadata(id).template Has<Tn...>();
            }
        }

        template<typename... Tn, typename LockType>
        inline bool Had(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            return lock.ReadMetadata(id).template Has<Tn...>();
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

            if constexpr (!std::is_const<ReturnType>()) lock.base->writeAccessedFlags.template Set<CompType>(true);

            auto &metadata = lock.permissions.HasGlobal() ? lock.WriteMetadata(id) : lock.ReadMetadata(id);
            if (!metadata.template Has<CompType>()) {
                if (is_add_remove_allowed<LockType>()) {
                    if (!metadata.template Has<>()) {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(id));
                    }
                    lock.base->writeAccessedFlags.SetGlobal(true);

                    // Reset value before allowing reading.
                    lock.instance.template Storage<CompType>().writeComponents[id.index] = {};
                    lock.instance.metadata.writeComponents[id.index].validComponents.template Set<CompType>(true);
                    auto &validEntities = lock.instance.template Storage<CompType>().writeValidEntities;
                    lock.instance.template Storage<CompType>().validEntityIndexes[id.index] = validEntities.size();
                    validEntities.emplace_back(*this);
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
                }
            }

            if (lock.permissions.template Has<CompType>()) {
                return lock.instance.template Storage<CompType>().writeComponents[id.index];
            } else {
                return lock.instance.template Storage<CompType>().readComponents[id.index];
            }
        }

        template<typename T, typename LockType>
        inline const T &GetPrevious(LockType &lock) const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(!is_global_component<CompType>(),
                "Global components must be accessed through lock.GetPrevious()");

            auto &metadata = lock.ReadMetadata(id);
            if (!metadata.template Has<CompType>()) {
                if (!metadata.template Has<>()) {
                    throw std::runtime_error("Entity does not exist: " + std::to_string(id));
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
                }
            }
            return lock.instance.template Storage<CompType>().readComponents[id.index];
        }

        template<typename T, typename LockType>
        inline T &Set(LockType &lock, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.base->writeAccessedFlags.template Set<T>(true);

            auto &metadata = lock.WriteMetadata(id);
            if (!metadata.template Has<T>()) {
                if (is_add_remove_allowed<LockType>()) {
                    if (!metadata.template Has<>()) {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(id));
                    }
                    lock.base->writeAccessedFlags.SetGlobal(true);

                    lock.instance.metadata.writeComponents[id.index].validComponents.template Set<T>(true);
                    auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                    lock.instance.template Storage<T>().validEntityIndexes[id.index] = validEntities.size();
                    validEntities.emplace_back(*this);
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            return lock.instance.template Storage<T>().writeComponents[id.index] = value;
        }

        template<typename T, typename LockType, typename... Args>
        inline T &Set(LockType &lock, Args... args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.base->writeAccessedFlags.template Set<T>(true);

            auto &metadata = lock.WriteMetadata(id);
            if (!metadata.template Has<T>()) {
                if (is_add_remove_allowed<LockType>()) {
                    if (!metadata.template Has<>()) {
                        throw std::runtime_error("Entity does not exist: " + std::to_string(id));
                    }
                    lock.base->writeAccessedFlags.SetGlobal(true);

                    lock.instance.metadata.writeComponents[id.index].validComponents.template Set<T>(true);
                    auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                    lock.instance.template Storage<T>().validEntityIndexes[id.index] = validEntities.size();
                    validEntities.emplace_back(*this);
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            return lock.instance.template Storage<T>().writeComponents[id.index] = T(std::forward<Args>(args)...);
        }

        template<typename... Tn, typename LockType>
        inline void Unset(LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");
            static_assert(!contains_global_components<Tn...>(),
                "Global components must be removed through lock.Unset()");

            auto &metadata = lock.WriteMetadata(id);
            if (!metadata.template Has<>()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            (lock.template RemoveComponents<Tn>(id.index), ...);
        }

        template<typename LockType>
        inline void Destroy(LockType &lock) {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be destroyed without an AddRemove lock.");
            lock.base->writeAccessedFlags.SetGlobal(true);

            auto &metadata = lock.WriteMetadata(id);
            if (!metadata.template Has<>()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            // It is possible for *this to be a reference to a component's writeValidEntities list
            // Copy the index before removing components so we can complete the removal.
            size_t index = id.index;

            // Invalidate the entity and all of its Components
            lock.RemoveAllComponents(index);
            lock.instance.metadata.writeComponents[index] = lock.instance.EmptyMetadataRef();
            size_t validIndex = lock.instance.metadata.validEntityIndexes[index];
            lock.instance.metadata.writeValidEntities[validIndex] = Entity();
            id = EntityId();
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
            return e.id.index;
        }
    };
} // namespace std
