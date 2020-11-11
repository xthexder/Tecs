#pragma once

#include "Tecs_template_util.hh"

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

        // Alias lock.Had<Tn...>(e) to allow e.Had<Tn...>(lock)
        template<typename... Tn, typename LockType>
        inline bool Had(LockType &lock) const {
            return lock.template Had<Tn...>(*this);
        }

        // Alias lock.Has<Tn...>(e) to allow e.Has<Tn...>(lock)
        template<typename... Tn, typename LockType>
        inline bool Has(LockType &lock) const {
            return lock.template Has<Tn...>(*this);
        }

        // Alias lock.Get<T>(e) to allow e.Get<T>(lock)
        // Includes a lock type check to make errors more readable.
        template<typename T, template<typename, typename...> typename LockType, typename ECSType, typename FirstLocked,
            typename... MoreLocked>
        inline decltype(auto) Get(LockType<ECSType, FirstLocked, MoreLocked...> &lock) const {
            // static_assert(contains<T, FirstLocked, MoreLocked...>::value, "Component type is not locked.");

            return lock.template Get<T>(*this);
        }

        template<typename T, template<typename> typename LockType, typename ECSType>
        inline decltype(auto) Get(LockType<ECSType> &lock) const {
            return lock.template Get<T>(*this);
        }

        // Alias lock.GetPrevious<T>(e) to allow e.GetPrevious<T>(lock)
        // Includes a lock type check to make errors more readable.
        template<typename T, template<typename, typename...> typename LockType, typename ECSType, typename FirstLocked,
            typename... MoreLocked>
        inline const T &GetPrevious(LockType<ECSType, FirstLocked, MoreLocked...> &lock) const {
            // static_assert(contains<T, FirstLocked, MoreLocked...>::value, "Component type is not locked.");

            return lock.template GetPrevious<T>(*this);
        }

        template<typename T, template<typename> typename LockType, typename ECSType>
        inline const T &GetPrevious(LockType<ECSType> &lock) const {
            return lock.template GetPrevious<T>(*this);
        }

        // Alias lock.Set<T>(e, T(args...)) to allow e.Set<T>(lock, T(args...))
        // Includes a lock type check to make errors more readable.
        template<typename T, template<typename, typename...> typename LockType, typename ECSType, typename FirstLocked,
            typename... MoreLocked>
        inline T &Set(LockType<ECSType, FirstLocked, MoreLocked...> &lock, T &value) const {
            // static_assert(contains<T, FirstLocked, MoreLocked...>::value, "Component type is not locked.");

            return lock.template Set<T>(*this, value);
        }

        template<typename T, template<typename> typename LockType, typename ECSType>
        inline T &Set(LockType<ECSType> &lock, T &value) const {
            return lock.template Set<T>(*this, value);
        }

        // Alias lock.Set<T>(e, args...) to allow e.Set<T>(lock, args...)
        // Includes a lock type check to make errors more readable.
        template<typename T, template<typename, typename...> typename LockType, typename ECSType, typename FirstLocked,
            typename... MoreLocked, typename... Args>
        inline T &Set(LockType<ECSType, FirstLocked, MoreLocked...> &lock, Args... args) const {
            // static_assert(contains<T, FirstLocked, MoreLocked...>::value, "Component type is not locked.");

            return lock.template Set<T>(*this, args...);
        }

        template<typename T, template<typename> typename LockType, typename ECSType, typename... Args>
        inline T &Set(LockType<ECSType> &lock, Args... args) const {
            return lock.template Set<T>(*this, args...);
        }

        // Alias lock.Unset<T>(e) to allow e.Unset<T>(lock)
        template<typename T, template<typename> typename LockType, typename ECSType>
        inline void Unset(LockType<ECSType> &lock) const {
            lock.template Unset<T>(*this);
        }
    };
} // namespace Tecs
