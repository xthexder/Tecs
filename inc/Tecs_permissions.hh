#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Tecs {
    template<typename... Tn>
    class ECS;
    template<typename, typename...>
    class Lock {};
    template<typename>
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

    /**
     * Lock permissions can be checked at runtime by wrapping permissions in an Optional<> type.
     *
     * Examples:
     * Lock<ECSType, Read<A, B>, Write<C>> lock = ecs.StartTransaction<Read<A, B>, Write<C>>();
     * // Optional locks can be created from existing required permissions.
     * Lock<ECSType, Read<A, Optional<B>>, Optional<Write<C>>> optionalLock = lock;
     *
     * // Required permissions will override optional permissions if they overlap.
     * Lock<ECSType, Read<A, B>, Optional<Read<B>>> == Lock<ECSType, Read<A, B>>;
     *
     * // Only required permissions can be accessed statically.
     * Lock<ECSType, Read<A>> lockA = optionalLock; // Valid
     * Lock<ECSType, Read<A, B>> lockAB = optionalLock; // Invalid
     *
     * // Optional permissions can be accessed at runtime using the TryLock() method.
     * std::optional<Lock<ECSType, Read<A, B>>> runtimeLock = optionalLock.TryLock<Read<A, B>>();
     * if (runtimeLock) {
     *     // Lock is available.
     * } else {
     *     // Lock is not available.
     * }
     */
    template<typename... OptionalPermissions>
    struct Optional {};

    /**
     * When a component is marked as global by this type trait, it can be accessed without referencing an entity.
     * Only a single instance of the component will be stored.
     *
     * This type trait can be set using the following pattern:
     *
     * template<>
     * struct Tecs::is_global_component<ComponentType> : std::true_type {};
     *
     * Or alternatively with the helper macro:
     *
     * TECS_GLOBAL_COMPONENT(ComponentType);
     *
     * Note: This must be defined in the root namespace only.
     */
    template<typename T>
    struct is_global_component : std::false_type {};

#define TECS_GLOBAL_COMPONENT(ComponentType)                                                                           \
    template<>                                                                                                         \
    struct Tecs::is_global_component<ComponentType> : std::true_type {};

    /**
     * Components can be named so they appear with the correct name in performance traces.
     * The component name type trait can be set using the following pattern:
     *
     * template<>
     * struct Tecs::component_name<ComponentType> {
     *     static constexpr char value[] = "ComponentName";
     * };
     *
     * Or alternatively with the helper macro:
     *
     * TECS_NAME_COMPONENT(ComponentType, "ComponentName");
     *
     * Note: This must be defined in the root namespace only.
     */
    template<typename T>
    struct component_name {
        static constexpr char value[] = "";
    };

#define TECS_NAME_COMPONENT(ComponentType, ComponentName)                                                              \
    template<>                                                                                                         \
    struct Tecs::component_name<ComponentType> {                                                                       \
        static constexpr char value[] = (ComponentName);                                                               \
    };

    // contains<T, Un...>::value is true if T is part of the set Un...
    template<typename T, typename... Un>
    struct contains : std::disjunction<std::is_same<T, Un>...> {};

    template<typename... Tn>
    struct contains_global_components : std::disjunction<is_global_component<Tn>...> {};
    template<typename... Tn>
    struct all_global_components : std::conjunction<is_global_component<Tn>...> {};

    /*
     * Compile time helpers for determining lock permissions.
     */
    template<typename T, typename Lock>
    struct is_read_allowed : std::false_type {};
    template<typename T, typename Lock>
    struct is_write_allowed : std::false_type {};
    template<typename Lock>
    struct is_add_remove_allowed : std::false_type {};

    template<typename T, typename Lock>
    struct is_optional : std::false_type {};
    template<typename T, typename Lock>
    struct is_read_optional : std::false_type {};
    template<typename T, typename Lock>
    struct is_write_optional : std::false_type {};
    template<typename Lock>
    struct is_add_remove_optional : std::false_type {};

    // Lock<Permissions...> specializations
    // clang-format off
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, Lock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, const Lock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};

    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, Lock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, const Lock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};

    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<Lock<ECSType, Permissions...>> : contains<AddRemove, Permissions...> {};
    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<const Lock<ECSType, Permissions...>> : contains<AddRemove, Permissions...> {};

    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_optional<T, Lock<ECSType, Permissions...>> : std::disjunction<is_read_optional<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_optional<T, const Lock<ECSType, Permissions...>> : std::disjunction<is_read_optional<T, Permissions>...> {};

    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_optional<T, Lock<ECSType, Permissions...>> : std::disjunction<is_write_optional<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_optional<T, const Lock<ECSType, Permissions...>> : std::disjunction<is_write_optional<T, Permissions>...> {};

    template<typename ECSType, typename... Permissions>
    struct is_add_remove_optional<Lock<ECSType, Permissions...>> : std::disjunction<is_add_remove_optional<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_add_remove_optional<const Lock<ECSType, Permissions...>> : std::disjunction<is_add_remove_optional<Permissions>...> {};

    // Optional<Permissions...> specializations
    template<typename T, typename... OptionalTypes>
    struct is_optional<T, Optional<OptionalTypes...>> : contains<T, OptionalTypes...> {};
    template<typename T, typename... Permissions>
    struct is_read_optional<T, Optional<Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, typename... Permissions>
    struct is_write_optional<T, Optional<Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};
    template<typename... Permissions>
    struct is_add_remove_optional<Optional<Permissions...>> : std::disjunction<is_add_remove_allowed<Permissions>...> {};
    // clang-format on

    // Check SubLock <= Lock for component type T
    template<typename T, typename SubLock, typename Lock>
    struct is_lock_subset
        : std::conjunction<
              std::conditional_t<is_write_allowed<T, SubLock>::value, is_write_allowed<T, Lock>, std::true_type>,
              std::conditional_t<is_read_allowed<T, SubLock>::value, is_read_allowed<T, Lock>, std::true_type>> {};

    // Read<LockedTypes...> specialization
    template<typename T, typename... LockedTypes>
    struct is_read_allowed<T, Read<LockedTypes...>> : contains<T, LockedTypes...> {};

    template<typename T, typename... LockedTypes>
    struct is_read_optional<T, Read<LockedTypes...>>
        : std::conjunction<std::negation<is_read_allowed<T, Read<LockedTypes...>>>,
              std::disjunction<is_optional<T, LockedTypes>...>> {};

    // ReadAll specialization
    template<typename T>
    struct is_read_allowed<T, ReadAll> : std::true_type {};

    // Write<LockedTypes...> specialization
    template<typename T, typename... LockedTypes>
    struct is_read_allowed<T, Write<LockedTypes...>> : contains<T, LockedTypes...> {};
    template<typename T, typename... LockedTypes>
    struct is_write_allowed<T, Write<LockedTypes...>> : contains<T, LockedTypes...> {};

    template<typename T, typename... LockedTypes>
    struct is_read_optional<T, Write<LockedTypes...>>
        : std::conjunction<std::negation<is_read_allowed<T, Write<LockedTypes...>>>,
              std::disjunction<is_optional<T, LockedTypes>...>> {};
    template<typename T, typename... LockedTypes>
    struct is_write_optional<T, Write<LockedTypes...>>
        : std::conjunction<std::negation<is_write_allowed<T, Write<LockedTypes...>>>,
              std::disjunction<is_optional<T, LockedTypes>...>> {};

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

    // Helpers for converting a lock type to a de-duplicated permission list
    template<typename... Permissions>
    struct TransactionPermissions {
        static constexpr const char *Name() {
            return __FUNCTION__;
        }
    };

    template<typename>
    struct tuple_to_read {
        using type = Read<>;
    };
    template<typename... Tn>
    struct tuple_to_read<std::tuple<Tn...>> {
        using type = Read<std::remove_pointer_t<Tn>...>;
    };

    template<typename>
    struct tuple_to_write {
        using type = Write<>;
    };
    template<typename... Tn>
    struct tuple_to_write<std::tuple<Tn...>> {
        using type = Write<std::remove_pointer_t<Tn>...>;
    };

    template<typename LockType, typename... AllComponentTypes>
    struct FlattenPermissions {
        using AllTuple = std::tuple<AllComponentTypes...>;

        template<size_t... Indices>
        static constexpr auto flatten_read(std::index_sequence<Indices...>) {
            // clang-format off
            return std::tuple_cat(std::conditional_t<
                is_read_allowed<std::tuple_element_t<Indices, AllTuple>, LockType>::value
                && !is_write_allowed<std::tuple_element_t<Indices, AllTuple>, LockType>::value,
                std::tuple<std::tuple_element_t<Indices, AllTuple> *>,
                std::tuple<>
            >{}...);
            // clang-format on
        }

        template<size_t... Indices>
        static constexpr auto flatten_write(std::index_sequence<Indices...>) {
            // clang-format off
            return std::tuple_cat(std::conditional_t<
                is_write_allowed<std::tuple_element_t<Indices, AllTuple>, LockType>::value,
                std::tuple<std::tuple_element_t<Indices, AllTuple> *>,
                std::tuple<>
            >{}...);
            // clang-format on
        }

        static constexpr auto flatten() {
            if constexpr (is_add_remove_allowed<LockType>()) {
                return TransactionPermissions<AddRemove>{};
            } else {
                using ReadPerm = decltype(flatten_read(std::make_index_sequence<sizeof...(AllComponentTypes)>()));
                using WritePerm = decltype(flatten_write(std::make_index_sequence<sizeof...(AllComponentTypes)>()));
                if constexpr (std::is_same_v<ReadPerm, std::tuple<>>) {
                    if constexpr (std::is_same_v<WritePerm, std::tuple<>>) {
                        return TransactionPermissions<>{};
                    } else {
                        return TransactionPermissions<typename tuple_to_write<WritePerm>::type>{};
                    }
                } else {
                    if constexpr (std::is_same_v<WritePerm, std::tuple<>>) {
                        return TransactionPermissions<typename tuple_to_read<ReadPerm>::type>{};
                    } else {
                        return TransactionPermissions<typename tuple_to_read<ReadPerm>::type,
                            typename tuple_to_write<WritePerm>::type>{};
                    }
                }
            }
        }

        using type = decltype(flatten());
    };
}; // namespace Tecs
