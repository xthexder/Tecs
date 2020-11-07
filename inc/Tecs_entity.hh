#pragma once

#include <cstddef>
#include <limits>

namespace Tecs {
    struct Entity {
        size_t id;

        inline Entity() : id(std::numeric_limits<decltype(id)>::max()) {}
        inline Entity(size_t id) : id(id) {}

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

        template<typename... Tn, typename LockType>
        inline bool Had(LockType &lock) const {
            return lock.template Had<Tn...>(*this);
        }

        template<typename... Tn, typename LockType>
        inline bool Has(LockType &lock) const {
            return lock.template Has<Tn...>(*this);
        }

        template<typename T, typename LockType>
        inline decltype(auto) Get(LockType &lock) const {
            return lock.template Get<T>(*this);
        }

        template<typename T, typename LockType>
        inline const T &GetPrevious(LockType &lock) const {
            return lock.template GetPrevious<T>(*this);
        }

        template<typename T, typename LockType>
        inline void Set(LockType &lock, T &value) const {
            lock.template Set<T>(*this, value);
        }

        template<typename T, typename LockType, typename... Args>
        inline void Set(LockType &lock, Args... args) const {
            lock.template Set<T>(*this, args...);
        }

        template<typename T, typename LockType>
        inline void Unset(LockType &lock) const {
            lock.template Unset<T>(*this);
        }
    };
} // namespace Tecs
