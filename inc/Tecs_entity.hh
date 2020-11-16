#pragma once

#include "Tecs_locks.hh"
#include "Tecs_template_util.hh"

#include <cstddef>
#include <functional>
#include <limits>

#ifndef TECS_ENTITY_ID_TYPE
    #define TECS_ENTITY_ID_TYPE size_t
#endif

namespace Tecs {
    struct Entity {
        TECS_ENTITY_ID_TYPE id;

        inline Entity() : id(std::numeric_limits<decltype(id)>::max()) {}
        inline Entity(decltype(id) id) : id(id) {}

        inline bool operator==(const Entity &other) const {
            return id == other.id;
        }

        inline bool operator!=(const Entity &other) const {
            return id != other.id;
        }

        inline bool operator!() const {
            return id == std::numeric_limits<decltype(id)>::max();
        }

        inline operator bool() const {
            return id != std::numeric_limits<decltype(id)>::max();
        }

        inline operator decltype(id)() const {
            return id;
        }

        // Alias lock.Has<Tn...>(e) to allow e.Has<Tn...>(lock)
        template<typename... Tn, typename LockType>
        inline bool Has(LockType &lock) const {
            return lock.template Has<Tn...>(*this);
        }

        // Alias lock.Had<Tn...>(e) to allow e.Had<Tn...>(lock)
        template<typename... Tn, typename LockType>
        inline bool Had(LockType &lock) const {
            return lock.template Had<Tn...>(*this);
        }

        // Alias lock.Get<T>(e) to allow e.Get<T>(lock)
        // Includes a lock type check to make errors more readable.
        template<typename T, typename LockType,
            typename ReturnType = std::conditional_t<is_write_allowed<T, LockType>::value, T, const T>>
        inline ReturnType &Get(LockType &lock) const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");
            static_assert(is_write_allowed<T, LockType>() || std::is_const<ReturnType>(),
                "Can't get non-const reference of read only Component.");

            return lock.template Get<T>(*this);
        }

        // Alias lock.GetPrevious<T>(e) to allow e.GetPrevious<T>(lock)
        // Includes a lock type check to make errors more readable.
        template<typename T, typename LockType>
        inline const T &GetPrevious(LockType &lock) const {
            static_assert(is_read_allowed<T, LockType>(), "Component is not locked for reading.");

            return lock.template GetPrevious<T>(*this);
        }

        // Alias lock.Set<T>(e, T(args...)) to allow e.Set<T>(lock, T(args...))
        // Includes a lock type check to make errors more readable.
        template<typename T, typename LockType>
        inline T &Set(LockType &lock, T &value) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");

            return lock.template Set<T>(*this, value);
        }

        // Alias lock.Set<T>(e, args...) to allow e.Set<T>(lock, args...)
        // Includes a lock type check to make errors more readable.
        template<typename T, typename LockType, typename... Args>
        inline T &Set(LockType &lock, Args... args) const {
            static_assert(is_write_allowed<T, LockType>(), "Component is not locked for writing.");

            return lock.template Set<T>(*this, args...);
        }

        // Alias lock.Unset<T>(e) to allow e.Unset<T>(lock)
        template<typename T, typename LockType>
        inline void Unset(LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Components cannot be removed without an AddRemove lock.");

            lock.template Unset<T>(*this);
        }

        // Alias lock.DestroyEntity(e) to allow e.Destroy(lock)
        template<typename LockType>
        inline void Destroy(LockType &lock) const {
            static_assert(is_add_remove_allowed<LockType>(), "Entities cannot be removed without an AddRemove lock.");

            lock.DestroyEntity(*this);
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
