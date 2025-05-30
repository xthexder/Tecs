#pragma once

#include "../Tecs_permissions.hh"
#include "Tecs_entity.h"

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

namespace Tecs::abi {
    struct Entity;

    extern thread_local size_t cacheInvalidationCounter;
}; // namespace Tecs::abi

namespace std {
    inline string to_string(const Tecs::abi::Entity &ent);
};

namespace Tecs::abi {
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
            : generation(eid >> (sizeof(TECS_ENTITY_INDEX_TYPE) * 8)),
              index(eid & std::numeric_limits<TECS_ENTITY_INDEX_TYPE>::max()) {}
        inline Entity(TECS_ENTITY_INDEX_TYPE index, TECS_ENTITY_GENERATION_TYPE generation)
            : index(index), generation(generation) {}
        inline Entity(TECS_ENTITY_INDEX_TYPE index, TECS_ENTITY_GENERATION_TYPE generation,
            TECS_ENTITY_ECS_IDENTIFIER_TYPE ecsId)
            : index(index), generation(GenerationWithIdentifier(generation, ecsId)) {}

    public:
        template<typename LockType>
        inline bool Exists(const LockType &lock) const {
            return Tecs_entity_exists(lock.base.get(), (TecsEntity)(*this));
        }

        template<typename LockType>
        inline bool Existed(const LockType &lock) const {
            return Tecs_entity_existed(lock.base.get(), (TecsEntity)(*this));
        }

        template<typename... Tn, typename LockType>
        inline bool Has(const LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            if constexpr (LockType::ECS::GetComponentCount() < std::numeric_limits<uint64_t>::digits) {
                std::bitset<1 + LockType::ECS::GetComponentCount()> componentBits;
                componentBits[0] = true;
                ((componentBits[1 + LockType::ECS::template GetComponentIndex<Tn>()] = true), ...);
                return Tecs_entity_has_bitset(lock.base.get(), (TecsEntity)(*this), componentBits.to_ullong());
            } else {
                return (Tecs_entity_has(lock.base.get(),
                            (TecsEntity)(*this),
                            LockType::ECS::template GetComponentIndex<Tn>()) &&
                        ...);
            }
        }

        template<typename... Tn, typename LockType>
        inline bool Had(const LockType &lock) const {
            static_assert(!contains_global_components<Tn...>(), "Entities cannot have global components");

            if constexpr (LockType::ECS::GetComponentCount() < std::numeric_limits<uint64_t>::digits) {
                std::bitset<1 + LockType::ECS::GetComponentCount()> componentBits;
                componentBits[0] = true;
                ((componentBits[1 + LockType::ECS::template GetComponentIndex<Tn>()] = true), ...);
                return Tecs_entity_had_bitset(lock.base.get(), (TecsEntity)(*this), componentBits.to_ullong());
            } else {
                return (Tecs_entity_had(lock.base.get(),
                            (TecsEntity)(*this),
                            LockType::ECS::template GetComponentIndex<Tn>()) &&
                        ...);
            }
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

            if (cacheInvalidationCounter != lock.cacheCounter) {
                lock.cachedStorage = {};
                lock.cachedConstStorage = {};
                lock.cachedPreviousStorage = {};
                lock.cacheCounter = cacheInvalidationCounter;
            }

            constexpr size_t componentIndex = LockType::ECS::template GetComponentIndex<CompType>();
            if constexpr (std::is_const<ReturnType>()) {
                auto *&cachedConstStorage = std::get<const CompType *>(lock.cachedConstStorage);
                if (!cachedConstStorage) {
                    cachedConstStorage =
                        static_cast<const CompType *>(Tecs_const_get_entity_storage(lock.base.get(), componentIndex));
                }
                return cachedConstStorage[index];
            } else {
                auto *&cachedStorage = std::get<CompType *>(lock.cachedStorage);
                if (!cachedStorage) {
                    cachedStorage = static_cast<CompType *>(Tecs_get_entity_storage(lock.base.get(), componentIndex));
                }
                return cachedStorage[index];
            }
        }

        template<typename T, typename LockType>
        inline const T &GetPrevious(const LockType &lock) const {
            using CompType = std::remove_cv_t<T>;
            static_assert(is_read_allowed<CompType, LockType>(), "Component is not locked for reading.");
            static_assert(!is_global_component<CompType>(),
                "Global components must be accessed through lock.GetPrevious()");

            if (cacheInvalidationCounter != lock.cacheCounter) {
                lock.cachedStorage = {};
                lock.cachedConstStorage = {};
                lock.cachedPreviousStorage = {};
                lock.cacheCounter = cacheInvalidationCounter;
            }
            auto *&cachedPreviousStorage = std::get<const CompType *>(lock.cachedPreviousStorage);
            if (!cachedPreviousStorage) {
                constexpr size_t componentIndex = LockType::ECS::template GetComponentIndex<CompType>();
                cachedPreviousStorage =
                    static_cast<CompType *>(Tecs_get_previous_entity_storage(lock.base.get(), componentIndex));
            }

            return cachedPreviousStorage[index];
        }

        template<typename T, typename LockType>
        inline T &Set(const LockType &lock, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");

            constexpr size_t componentIndex = LockType::ECS::template GetComponentIndex<T>();
            return *static_cast<T *>(Tecs_entity_set(lock.base.get(), (TecsEntity)(*this), componentIndex, &value));
        }

        template<typename T, typename LockType, typename... Args>
        inline T &Set(const LockType &lock, Args... args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");
            static_assert(!is_global_component<T>(), "Global components must be accessed through lock.Set()");

            T temp = T(std::forward<Args>(args)...);

            constexpr size_t componentIndex = LockType::ECS::template GetComponentIndex<T>();
            return *static_cast<T *>(Tecs_entity_set(lock.base.get(), (TecsEntity)(*this), componentIndex, &temp));
        }

        template<typename... Tn, typename LockType>
        inline void Unset(const LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");
            static_assert(!contains_global_components<Tn...>(),
                "Global components must be removed through lock.Unset()");

            (Tecs_entity_unset(lock.base.get(), (TecsEntity)(*this), LockType::ECS::template GetComponentIndex<Tn>()),
                ...);
        }

        template<typename LockType>
        inline void Destroy(const LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be destroyed without an AddRemove lock.");

            Tecs_entity_destroy(lock.base.get(), (TecsEntity)(*this));
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
} // namespace Tecs::abi

namespace std {
    template<>
    struct hash<Tecs::abi::Entity> {
        std::size_t operator()(const Tecs::abi::Entity &e) const {
            const auto genBits = sizeof(TECS_ENTITY_GENERATION_TYPE) * 8;
            uint64_t value = (uint64_t)e.index << genBits | e.generation;
            return hash<uint64_t>{}(value);
        }
    };

    inline string to_string(const Tecs::abi::Entity &ent) {
        if (ent) {
            auto ecsId = Tecs::abi::IdentifierFromGeneration(ent.generation);
            auto generation = Tecs::abi::GenerationWithoutIdentifier(ent.generation);
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
