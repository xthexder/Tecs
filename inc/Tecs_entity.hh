#pragma once

#include "Tecs_permissions.hh"

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>

#ifndef TECS_ENTITY_INDEX_TYPE
    #define TECS_ENTITY_INDEX_TYPE uint32_t
#endif

#ifndef TECS_ENTITY_GENERATION_TYPE
    #define TECS_ENTITY_GENERATION_TYPE uint32_t
#endif

#ifndef TECS_ENTITY_ECS_IDENTIFIER_TYPE
    #define TECS_ENTITY_ECS_IDENTIFIER_TYPE uint8_t
#endif

static_assert(sizeof(TECS_ENTITY_GENERATION_TYPE) > sizeof(TECS_ENTITY_ECS_IDENTIFIER_TYPE),
    "TECS_ENTITY_ECS_IDENTIFIER_TYPE must fit within TECS_ENTITY_GENERATION_TYPE");

namespace Tecs {
    struct Entity;
};

namespace std {
    inline string to_string(const Tecs::Entity &ent);
};

namespace Tecs {
    constexpr TECS_ENTITY_GENERATION_TYPE GenerationWithoutIdentifier(TECS_ENTITY_GENERATION_TYPE generation) {
        constexpr size_t generationBits =
            (sizeof(TECS_ENTITY_GENERATION_TYPE) - sizeof(TECS_ENTITY_ECS_IDENTIFIER_TYPE)) * 8;
        constexpr auto generationMask = ((TECS_ENTITY_GENERATION_TYPE)1 << generationBits) - 1;
        return generation & generationMask;
    }

    constexpr TECS_ENTITY_GENERATION_TYPE GenerationWithIdentifier(TECS_ENTITY_GENERATION_TYPE generation,
        TECS_ENTITY_ECS_IDENTIFIER_TYPE ecsId) {
        constexpr size_t generationBits =
            (sizeof(TECS_ENTITY_GENERATION_TYPE) - sizeof(TECS_ENTITY_ECS_IDENTIFIER_TYPE)) * 8;
        return GenerationWithoutIdentifier(generation) | ((TECS_ENTITY_GENERATION_TYPE)ecsId << generationBits);
    }

    constexpr TECS_ENTITY_ECS_IDENTIFIER_TYPE IdentifierFromGeneration(TECS_ENTITY_GENERATION_TYPE generation) {
        constexpr size_t generationBits =
            (sizeof(TECS_ENTITY_GENERATION_TYPE) - sizeof(TECS_ENTITY_ECS_IDENTIFIER_TYPE)) * 8;
        return (TECS_ENTITY_ECS_IDENTIFIER_TYPE)(generation >> generationBits);
    }

    struct Entity {
        // Workaround for Clang so that std::atomic<Tecs::Entity> operations can be inlined as if uint64. See issue:
        // https://stackoverflow.com/questions/60445848/clang-doesnt-inline-stdatomicload-for-loading-64-bit-structs
        alignas(sizeof(TECS_ENTITY_GENERATION_TYPE) +
                sizeof(TECS_ENTITY_INDEX_TYPE)) TECS_ENTITY_GENERATION_TYPE generation;
        TECS_ENTITY_INDEX_TYPE index;

        inline Entity() : generation(0), index(0) {}
        inline Entity(TECS_ENTITY_INDEX_TYPE index, TECS_ENTITY_GENERATION_TYPE generation)
            : generation(generation), index(index) {}
        inline Entity(TECS_ENTITY_INDEX_TYPE index, TECS_ENTITY_GENERATION_TYPE generation,
            TECS_ENTITY_ECS_IDENTIFIER_TYPE ecsId)
            : generation(GenerationWithIdentifier(generation, ecsId)), index(index) {}

    public:
        template<typename LockType>
        inline bool Exists(LockType &lock) const {
            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) return false;

            auto &metadata = metadataList[index];
            return metadata[0] && metadata.generation == generation;
        }

        template<typename LockType>
        inline bool Existed(LockType &lock) const {
            if (index >= lock.instance.metadata.readComponents.size()) return false;

            auto &metadata = lock.instance.metadata.readComponents[index];
            return metadata[0] && metadata.generation == generation;
        }

        template<typename... Tn, typename LockType>
        inline bool Has(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");
            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) return false;

            auto &metadata = metadataList[index];
            return metadata[0] && metadata.generation == generation &&
                   lock.instance.template BitsetHas<Tn...>(metadata);
        }

        template<typename... Tn, typename LockType>
        inline bool Had(LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");
            if (index >= lock.instance.metadata.readComponents.size()) return false;

            auto &metadata = lock.instance.metadata.readComponents[index];
            return metadata[0] && metadata.generation == generation &&
                   lock.instance.template BitsetHas<Tn...>(metadata);
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

            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            auto &metadata = metadataList[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            if (!lock.instance.template BitsetHas<CompType>(metadata)) {
                if (is_add_remove_allowed<LockType>() && !std::is_const<ReturnType>()) {
                    lock.base->writeAccessedFlags[0] = true;

                    // Reset value before allowing reading.
                    lock.instance.template Storage<CompType>().writeComponents[index] = {};
                    metadata[1 + lock.instance.template GetComponentIndex<CompType>()] = true;
                    auto &validEntities = lock.instance.template Storage<CompType>().writeValidEntities;
                    lock.instance.template Storage<CompType>().validEntityIndexes[index] = validEntities.size();
                    validEntities.emplace_back(*this);
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
                }
            }

            if (lock.instance.template BitsetHas<CompType>(lock.writePermissions)) {
                return lock.instance.template Storage<CompType>().writeComponents[index];
            } else {
                return lock.instance.template Storage<CompType>().readComponents[index];
            }
        }

        template<typename T, typename LockType>
        inline const T &GetPrevious(LockType &lock) const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(!is_global_component<CompType>(),
                "Global components must be accessed through lock.GetPrevious()");

            if (index >= lock.instance.metadata.readComponents.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            auto &metadata = lock.instance.metadata.readComponents[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            if (!lock.instance.template BitsetHas<CompType>(metadata)) {
                throw std::runtime_error(
                    "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
            }
            return lock.instance.template Storage<CompType>().readComponents[index];
        }

        template<typename T, typename LockType>
        inline T &Set(LockType &lock, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.base->template SetAccessFlag<T>(true);

            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            auto &metadata = metadataList[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            if (!lock.instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    lock.base->writeAccessedFlags[0] = true;

                    metadata[1 + lock.instance.template GetComponentIndex<T>()] = true;
                    auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                    lock.instance.template Storage<T>().validEntityIndexes[index] = validEntities.size();
                    validEntities.emplace_back(*this);
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            return lock.instance.template Storage<T>().writeComponents[index] = value;
        }

        template<typename T, typename LockType, typename... Args>
        inline T &Set(LockType &lock, Args... args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.base->template SetAccessFlag<T>(true);

            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            auto &metadata = metadataList[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            if (!lock.instance.template BitsetHas<T>(metadata)) {
                if (is_add_remove_allowed<LockType>()) {
                    lock.base->writeAccessedFlags[0] = true;

                    metadata[1 + lock.instance.template GetComponentIndex<T>()] = true;
                    auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                    lock.instance.template Storage<T>().validEntityIndexes[index] = validEntities.size();
                    validEntities.emplace_back(*this);
                } else {
                    throw std::runtime_error(
                        "Entity does not have a component of type: " + std::string(typeid(T).name()));
                }
            }
            return lock.instance.template Storage<T>().writeComponents[index] = T(std::forward<Args>(args)...);
        }

        template<typename... Tn, typename LockType>
        inline void Unset(LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");
            static_assert(!contains_global_components<Tn...>(),
                "Global components must be removed through lock.Unset()");

            if (index >= lock.instance.metadata.writeComponents.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
            auto &metadata = lock.instance.metadata.writeComponents[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            (lock.template RemoveComponents<Tn>(index), ...);
        }

        template<typename LockType>
        inline void Destroy(LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be destroyed without an AddRemove lock.");
            lock.base->writeAccessedFlags[0] = true;

            if (index >= lock.instance.metadata.writeComponents.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
            auto &metadata = lock.instance.metadata.writeComponents[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            // It is possible for *this to be a reference to a component's writeValidEntities list
            // Copy the index before removing components so we can complete the removal.
            size_t copy = index;

            // Invalidate the entity and all of its Components
            lock.RemoveAllComponents(copy);
            lock.instance.metadata.writeComponents[copy][0] = false;
            size_t validIndex = lock.instance.metadata.validEntityIndexes[copy];
            lock.instance.metadata.writeValidEntities[validIndex] = Entity();
        }

        template<typename LockType>
        inline void Destroy(LockType &lock) {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be destroyed without an AddRemove lock.");

            ((const Entity *)this)->Destroy(lock);

            generation = 0;
            index = 0;
        }

        inline bool operator==(const Entity &other) const {
            return index == other.index && generation == other.generation;
        }

        inline bool operator!=(const Entity &other) const {
            return index != other.index || generation != other.generation;
        }

        inline bool operator<(const Entity &other) const {
            return (index == other.index) ? (generation < other.generation) : (index < other.index);
        }

        inline bool operator!() const {
            return generation == 0;
        }

        inline explicit operator bool() const {
            return generation != 0;
        }
    };

    static_assert(sizeof(Entity) <= sizeof(uint64_t), "Tecs::Entity must not exceed 64 bits");
} // namespace Tecs

namespace std {
    template<>
    struct hash<Tecs::Entity> {
        std::size_t operator()(const Tecs::Entity &e) const {
            const auto genBits = sizeof(TECS_ENTITY_GENERATION_TYPE) * 8;
            uint64_t value = (uint64_t)e.index << genBits | e.generation;
            return hash<uint64_t>{}(value);
        }
    };

    inline string to_string(const Tecs::Entity &ent) {
        if (ent) {
            auto ecsId = Tecs::IdentifierFromGeneration(ent.generation);
            auto generation = Tecs::GenerationWithoutIdentifier(ent.generation);
            if (ecsId == 1) {
                // Simplify logging for the common case of 1 ECS instance.
                return "Entity(gen " + to_string(generation) + ", index " + to_string(ent.index) + ")";
            } else {
                return "Entity(ecs " + to_string(ecsId) + ", gen " + to_string(generation) + ", index " +
                       to_string(ent.index) + ")";
            }
        } else {
            return "Entity(invalid)";
        }
    }
} // namespace std
