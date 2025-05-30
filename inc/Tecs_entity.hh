#pragma once

#include "Tecs_permissions.hh"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
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
        alignas(sizeof(TECS_ENTITY_GENERATION_TYPE) + sizeof(TECS_ENTITY_INDEX_TYPE)) TECS_ENTITY_INDEX_TYPE index;
        TECS_ENTITY_GENERATION_TYPE generation;

        inline Entity() : index(0), generation(0) {}

        inline Entity(uint64_t eid)
            : index(eid & std::numeric_limits<TECS_ENTITY_INDEX_TYPE>::max()),
              generation(eid >> (sizeof(TECS_ENTITY_INDEX_TYPE) * 8)) {}
        inline Entity(TECS_ENTITY_INDEX_TYPE index, TECS_ENTITY_GENERATION_TYPE generation)
            : index(index), generation(generation) {}
        inline Entity(TECS_ENTITY_INDEX_TYPE index, TECS_ENTITY_GENERATION_TYPE generation,
            TECS_ENTITY_ECS_IDENTIFIER_TYPE ecsId)
            : index(index), generation(GenerationWithIdentifier(generation, ecsId)) {}

    public:
        template<typename LockType>
        inline bool Exists(const LockType &lock) const {
            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) return false;

            auto &metadata = metadataList[index];
            return metadata[0] && metadata.generation == generation;
        }

        template<typename LockType>
        inline bool Existed(const LockType &lock) const {
            if (index >= lock.instance.metadata.readComponents.size()) return false;

            auto &metadata = lock.instance.metadata.readComponents[index];
            return metadata[0] && metadata.generation == generation;
        }

        template<typename... Tn, typename LockType>
        inline bool Has(const LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");
            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) return false;

            auto &metadata = metadataList[index];
            return metadata[0] && metadata.generation == generation &&
                   lock.instance.template BitsetHas<Tn...>(metadata);
        }

        template<typename LockType,
            std::enable_if_t<(LockType::ECS::GetComponentCount() < std::numeric_limits<uint64_t>::digits), int> = 0>
        inline bool HasBitset(const LockType &lock,
            const std::bitset<1 + LockType::ECS::GetComponentCount()> &componentBits) const {
            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) return false;

            auto &metadata = metadataList[index];
            return metadata[0] && metadata.generation == generation && (metadata & componentBits) == componentBits;
        }

        template<typename... Tn, typename LockType>
        inline bool Had(const LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");
            if (index >= lock.instance.metadata.readComponents.size()) return false;

            auto &metadata = lock.instance.metadata.readComponents[index];
            return metadata[0] && metadata.generation == generation &&
                   lock.instance.template BitsetHas<Tn...>(metadata);
        }

        template<typename LockType,
            std::enable_if_t<(LockType::ECS::GetComponentCount() < std::numeric_limits<uint64_t>::digits), int> = 0>
        inline bool HadBitset(const LockType &lock,
            const std::bitset<1 + LockType::ECS::GetComponentCount()> &componentBits) const {
            if (index >= lock.instance.metadata.readComponents.size()) return false;

            auto &metadata = lock.instance.metadata.readComponents[index];
            return metadata[0] && metadata.generation == generation && (metadata & componentBits) == componentBits;
        }

        template<typename T, typename LockType,
            typename ReturnType =
                std::conditional_t<is_write_allowed<std::remove_cv_t<T>, LockType>::value, T, const T>>
        inline ReturnType &Get(const LockType &lock) const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(is_write_allowed<CompType, LockType>() || std::is_const<ReturnType>(),
                "Can't get non-const reference of read only Component.");
            static_assert(!is_global_component<CompType>(), "Global components must be accessed through lock.Get()");

            if constexpr (!std::is_const<ReturnType>()) lock.transaction->template SetAccessFlag<CompType>(index);

#ifndef TECS_UNCHECKED_MODE
            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            auto &metadata = metadataList[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
#endif

            auto &storage = lock.instance.template Storage<CompType>();

            if constexpr (is_add_remove_allowed<LockType>() && !std::is_const<ReturnType>()) {
#ifdef TECS_UNCHECKED_MODE
                auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                              : lock.instance.metadata.readComponents;
                auto &metadata = metadataList[index];
#endif
                if (!lock.instance.template BitsetHas<CompType>(metadata)) {
                    lock.transaction->template SetAccessFlag<AddRemove>();

                    // Reset value before allowing reading.
                    storage.writeComponents[index] = {};
                    metadata[1 + lock.instance.template GetComponentIndex<CompType>()] = true;
                    auto &validEntities = storage.writeValidEntities;
                    storage.validEntityIndexes[index] = validEntities.size();
                    validEntities.emplace_back(*this);
                }
#ifndef TECS_UNCHECKED_MODE
            } else if (!lock.instance.template BitsetHas<CompType>(metadata)) {
                throw std::runtime_error(
                    "Entity does not have a component of type: " + std::string(typeid(CompType).name()));
#endif
            }

            if (lock.instance.template BitsetHas<CompType>(lock.writePermissions)) {
                return storage.writeComponents[index];
            } else {
                return storage.readComponents[index];
            }
        }

        template<typename T, typename LockType>
        inline const T &GetPrevious(const LockType &lock) const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(!is_global_component<CompType>(),
                "Global components must be accessed through lock.GetPrevious()");

#ifndef TECS_UNCHECKED_MODE
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
#endif
            return lock.instance.template Storage<CompType>().readComponents[index];
        }

        template<typename T, typename LockType>
        inline T &Set(const LockType &lock, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.transaction->template SetAccessFlag<T>(index);

#ifndef TECS_UNCHECKED_MODE
            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            auto &metadata = metadataList[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
#endif

            if constexpr (is_add_remove_allowed<LockType>()) {
#ifdef TECS_UNCHECKED_MODE
                auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                              : lock.instance.metadata.readComponents;
                auto &metadata = metadataList[index];
#endif
                if (!lock.instance.template BitsetHas<T>(metadata)) {
                    lock.transaction->template SetAccessFlag<AddRemove>();

                    metadata[1 + lock.instance.template GetComponentIndex<T>()] = true;
                    auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                    lock.instance.template Storage<T>().validEntityIndexes[index] = validEntities.size();
                    validEntities.emplace_back(*this);
                }
#ifndef TECS_UNCHECKED_MODE
            } else if (!lock.instance.template BitsetHas<T>(metadata)) {
                throw std::runtime_error("Entity does not have a component of type: " + std::string(typeid(T).name()));
#endif
            }
            return lock.instance.template Storage<T>().writeComponents[index] = value;
        }

        template<typename T, typename LockType, typename... Args>
        inline T &Set(const LockType &lock, Args... args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");
            lock.transaction->template SetAccessFlag<T>(index);

#ifndef TECS_UNCHECKED_MODE
            auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                          : lock.instance.metadata.readComponents;
            if (index >= metadataList.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }

            auto &metadata = metadataList[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
#endif

            if constexpr (is_add_remove_allowed<LockType>()) {
#ifdef TECS_UNCHECKED_MODE
                auto &metadataList = lock.writePermissions[0] ? lock.instance.metadata.writeComponents
                                                              : lock.instance.metadata.readComponents;
                auto &metadata = metadataList[index];
#endif
                if (!lock.instance.template BitsetHas<T>(metadata)) {
                    lock.transaction->template SetAccessFlag<AddRemove>();

                    metadata[1 + lock.instance.template GetComponentIndex<T>()] = true;
                    auto &validEntities = lock.instance.template Storage<T>().writeValidEntities;
                    lock.instance.template Storage<T>().validEntityIndexes[index] = validEntities.size();
                    validEntities.emplace_back(*this);
                }
#ifndef TECS_UNCHECKED_MODE
            } else if (!lock.instance.template BitsetHas<T>(metadata)) {
                throw std::runtime_error("Entity does not have a component of type: " + std::string(typeid(T).name()));
#endif
            }
            return lock.instance.template Storage<T>().writeComponents[index] = T(std::forward<Args>(args)...);
        }

        template<typename... Tn, typename LockType>
        inline void Unset(const LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");
            static_assert(!contains_global_components<Tn...>(),
                "Global components must be removed through lock.Unset()");

#ifndef TECS_UNCHECKED_MODE
            if (index >= lock.instance.metadata.writeComponents.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
            auto &metadata = lock.instance.metadata.writeComponents[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
#endif

            (lock.template RemoveComponents<Tn>(index), ...);
        }

        template<typename LockType>
        inline void Destroy(const LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be destroyed without an AddRemove lock.");
            lock.transaction->template SetAccessFlag<AddRemove>();

#ifndef TECS_UNCHECKED_MODE
            if (index >= lock.instance.metadata.writeComponents.size()) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
            auto &metadata = lock.instance.metadata.writeComponents[index];
            if (!metadata[0] || metadata.generation != generation) {
                throw std::runtime_error("Entity does not exist: " + std::to_string(*this));
            }
#endif

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
        inline void Destroy(const LockType &lock) {
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

        inline explicit operator uint64_t() const {
            return (((uint64_t)generation) << (sizeof(TECS_ENTITY_INDEX_TYPE) * 8)) | (uint64_t)index;
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
