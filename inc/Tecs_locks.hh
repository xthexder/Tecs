#pragma once

#include "Tecs_template_util.hh"

#include <type_traits>

namespace Tecs {
    template<typename... Tn>
    class ECS;
    template<typename, typename...>
    class Lock {};
    template<typename, typename...>
    class Transaction {};

    /**
     * Lock permissions are passed in as template arguments when creating a Transaction or Lock.
     *
     * Examples:
     * using ECSType = Tecs::ECS<A, B, C>;
     *
     * // Allow read access to A and B components, and write access to C components.
     * Transaction<ECSType, Read<A, B>, Write<C>> transaction1 = ecs.NewTransaction<Read<A, B>, Write<C>>();
     *
     * // Allow read and write access to all components, as well as adding and removing entities and components.
     * Transaction<ECSType, AddRemove> transaction2 = ecs.NewTransaction<AddRemove>();
     *
     * // Reference a transaction's permissions (or a subset of them) by using the Lock type.
     * Lock<ECSType, AddRemove> lockAll = transaction2;
     * Lock<ECSType, Read<A>, Write<B>> lockWriteB = transaction1;
     *
     * // Locks can be automatically cast to subsets of existing locks.
     * Lock<ECSType, Read<A, B>> lockReadAB = lockWriteB;
     * Lock<ECSType, Read<B>> lockReadB = lockReadAB;
     */
    template<typename... LockedTypes>
    struct Read {};
    struct ReadAll {};
    template<typename... LockedTypes>
    struct Write {};
    struct WriteAll {};
    struct AddRemove {};

    /*
     * Compile time helpers for determining lock permissions.
     */
    template<typename T, typename Lock>
    struct is_read_allowed : std::false_type {};
    template<typename T, typename Lock>
    struct is_write_allowed : std::false_type {};
    template<typename Lock>
    struct is_add_remove_allowed : std::false_type {};

    // Lock<Permissions...> specializations
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, Lock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, Lock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {
    };
    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<Lock<ECSType, Permissions...>> : contains<AddRemove, Permissions...> {};

    // Check SubLock <= Lock for component type T
    template<typename T, typename SubLock, typename Lock>
    struct is_lock_subset
        : std::conjunction<
              std::conditional_t<is_write_allowed<T, SubLock>::value, is_write_allowed<T, Lock>, std::true_type>,
              std::conditional_t<is_read_allowed<T, SubLock>::value, is_read_allowed<T, Lock>, std::true_type>> {};

    // Read<LockedTypes...> specialization
    template<typename T, typename... LockedTypes>
    struct is_read_allowed<T, Read<LockedTypes...>> : contains<T, LockedTypes...> {};

    // ReadAll specialization
    template<typename T>
    struct is_read_allowed<T, ReadAll> : std::true_type {};

    // Write<LockedTypes...> specialization
    template<typename T, typename... LockedTypes>
    struct is_read_allowed<T, Write<LockedTypes...>> : contains<T, LockedTypes...> {};
    template<typename T, typename... LockedTypes>
    struct is_write_allowed<T, Write<LockedTypes...>> : contains<T, LockedTypes...> {};

    // WriteAll specialization
    template<typename T>
    struct is_read_allowed<T, WriteAll> : std::true_type {};
    template<typename T>
    struct is_write_allowed<T, WriteAll> : std::true_type {};

    // AddRemove specialization
    template<typename T>
    struct is_read_allowed<T, AddRemove> : std::true_type {};
    template<typename T>
    struct is_write_allowed<T, AddRemove> : std::true_type {};
    template<>
    struct is_add_remove_allowed<AddRemove> : std::true_type {};
}; // namespace Tecs
