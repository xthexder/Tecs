#pragma once

#include "Tecs_permissions.hh"

#include <cstddef>
#include <functional>
#include <stdexcept>

// Default to half the entity id bits being used for index, and half for generation count.
#ifndef TECS_ENTITY_INDEX_TYPE
    #define TECS_ENTITY_INDEX_TYPE uint32_t
#endif
#ifndef TECS_ENTITY_GENERATION_TYPE
    #define TECS_ENTITY_GENERATION_TYPE uint32_t
#endif

namespace Tecs {
    struct EntityId {
        TECS_ENTITY_INDEX_TYPE index = 0;
        TECS_ENTITY_GENERATION_TYPE generation = 0;

        EntityId() {}
        EntityId(TECS_ENTITY_INDEX_TYPE index, TECS_ENTITY_GENERATION_TYPE gen = 1) : index(index), generation(gen) {}
        EntityId(size_t index, TECS_ENTITY_GENERATION_TYPE gen = 1) : generation(gen) {
            if (index > std::numeric_limits<decltype(index)>::max()) {
                throw std::runtime_error("New entity index overflows index type: " + std::to_string(index));
            }
            this->index = (TECS_ENTITY_INDEX_TYPE)index;
        }

        inline bool operator==(const EntityId &other) const {
            return index == other.index && generation == other.generation;
        }

        inline bool operator!=(const EntityId &other) const {
            return !(*this == other);
        }

        inline bool operator<(const EntityId &other) const {
            return index < other.index;
        }

        inline bool operator!() const {
            return generation == 0;
        }

        inline explicit operator bool() const {
            return generation != 0;
        }
    };
    static_assert(sizeof(EntityId) == sizeof(uint64_t), "EntityId is too large!");
} // namespace Tecs

namespace std {
    inline string to_string(const Tecs::EntityId &id) {
        return "Id(" + to_string(id.generation) + ", " + to_string(id.index) + ")";
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
                return lock.WriteMetadata(id).template Has<>(id);
            } else {
                return lock.ReadMetadata(id).template Has<>(id);
            }
        }

        template<typename LockType>
        inline bool Existed(LockType &lock) const {
            return lock.ReadMetadata(id).template Has<>(id);
        }

        template<typename... Tn, typename LockType>
        inline bool Has(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            if (lock.permissions.HasGlobal()) {
                return lock.WriteMetadata(id).template Has<Tn...>(id);
            } else {
                return lock.ReadMetadata(id).template Has<Tn...>(id);
            }
        }

        template<typename... Tn, typename LockType>
        inline bool Had(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            return lock.ReadMetadata(id).template Has<Tn...>(id);
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
            if (!metadata.template Has<>(id)) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            if (!metadata.template Has<CompType>(id)) {
                if (is_add_remove_allowed<LockType>()) {
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
            if (!metadata.template Has<>(id)) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            if (!metadata.template Has<CompType>(id)) {
                throw std::runtime_error(
                    "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
            }
            return lock.instance.template Storage<CompType>().readComponents[id.index];
        }

        template<typename T, typename LockType>
        inline T &Set(LockType &lock, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.base->writeAccessedFlags.template Set<T>(true);

            auto &metadata = lock.WriteMetadata(id);
            if (!metadata.template Has<>(id)) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            if (!metadata.template Has<T>(id)) {
                if (is_add_remove_allowed<LockType>()) {
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
            if (!metadata.template Has<>(id)) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            if (!metadata.template Has<T>(id)) {
                if (is_add_remove_allowed<LockType>()) {
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
            if (!metadata.template Has<>(id)) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(id));
            }

            (lock.template RemoveComponents<Tn>(id.index), ...);
        }

        template<typename LockType>
        inline void Destroy(LockType &lock) {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be destroyed without an AddRemove lock.");
            lock.base->writeAccessedFlags.SetGlobal(true);

            auto &metadata = lock.WriteMetadata(id);
            if (!metadata.template Has<>(id)) {
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
            return e.id.index | ((size_t)e.id.generation << 32);
        }
    };
} // namespace std
