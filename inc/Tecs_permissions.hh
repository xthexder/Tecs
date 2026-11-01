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
    template<typename, typename...>
    class DynamicLock {};
    template<typename>
    class Transaction {};

    namespace abi {
        template<typename, typename...>
        class Lock {};
    } // namespace abi

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
    struct ReadPrevious {};

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
        static constexpr char value[] = ComponentName;                                                                 \
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
    template<typename Lock>
    struct is_read_previous_allowed : std::false_type {};

    // Lock<Permissions...> and DynamicLock<Permissions...> specializations
    // clang-format off
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, Lock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, const Lock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, DynamicLock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, const DynamicLock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, abi::Lock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_read_allowed<T, const abi::Lock<ECSType, Permissions...>> : std::disjunction<is_read_allowed<T, Permissions>...> {};

    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, Lock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, const Lock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, DynamicLock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, const DynamicLock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, abi::Lock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};
    template<typename T, typename ECSType, typename... Permissions>
    struct is_write_allowed<T, const abi::Lock<ECSType, Permissions...>> : std::disjunction<is_write_allowed<T, Permissions>...> {};

    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<Lock<ECSType, Permissions...>> : std::disjunction<is_add_remove_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<const Lock<ECSType, Permissions...>> : std::disjunction<is_add_remove_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<DynamicLock<ECSType, Permissions...>> : std::disjunction<is_add_remove_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<const DynamicLock<ECSType, Permissions...>> : std::disjunction<is_add_remove_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<abi::Lock<ECSType, Permissions...>> : std::disjunction<is_add_remove_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_add_remove_allowed<const abi::Lock<ECSType, Permissions...>> : std::disjunction<is_add_remove_allowed<Permissions>...> {};

    template<typename ECSType, typename... Permissions>
    struct is_read_previous_allowed<Lock<ECSType, Permissions...>> : std::disjunction<is_read_previous_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_read_previous_allowed<const Lock<ECSType, Permissions...>> : std::disjunction<is_read_previous_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_read_previous_allowed<DynamicLock<ECSType, Permissions...>> : std::disjunction<is_read_previous_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_read_previous_allowed<const DynamicLock<ECSType, Permissions...>> : std::disjunction<is_read_previous_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_read_previous_allowed<abi::Lock<ECSType, Permissions...>> : std::disjunction<is_read_previous_allowed<Permissions>...> {};
    template<typename ECSType, typename... Permissions>
    struct is_read_previous_allowed<const abi::Lock<ECSType, Permissions...>> : std::disjunction<is_read_previous_allowed<Permissions>...> {};
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
    template<>
    struct is_read_previous_allowed<ReadPrevious> : std::true_type {};

    // Helpers for converting a lock type to a de-duplicated permission list
    template<typename... Permissions>
    struct TransactionPermissions {
        static constexpr const char *Name() {
            return __FUNCTION__;
        }
    };

    namespace detail {
        template<typename>
        struct tuple_to {
            using read = Read<>;
            using write = Write<>;
        };
        template<typename... Tn>
        struct tuple_to<std::tuple<Tn...>> {
            using read = Read<std::remove_pointer_t<Tn>...>;
            using write = Write<std::remove_pointer_t<Tn>...>;
        };

        template<typename ECS, typename...>
        struct tuple_to_lock {
            using type = Lock<ECS>;
            using type_dynamic = DynamicLock<ECS>;
        };
        template<typename ECS, typename... Tn>
        struct tuple_to_lock<ECS, std::tuple<Tn...>> {
            using type = Lock<ECS, std::remove_pointer_t<Tn>...>;
            using type_dynamic = DynamicLock<ECS, std::remove_pointer_t<Tn>...>;
        };

        template<typename LockType, typename AllTuple, size_t... Indices>
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

        template<typename LockType, typename AllTuple, size_t... Indices>
        static constexpr auto flatten_readonly(std::index_sequence<Indices...>) {
            // clang-format off
            auto readonly = std::tuple_cat(std::conditional_t<
                is_read_allowed<std::tuple_element_t<Indices, AllTuple>, LockType>::value,
                std::tuple<std::tuple_element_t<Indices, AllTuple> *>,
                std::tuple<>
            >{}...);
            if constexpr (is_read_previous_allowed<LockType>()) {
                return std::tuple_cat(readonly, std::tuple<ReadPrevious>{});
            } else {
                return readonly;
            }
            // clang-format on
        }

        template<typename LockType, typename AllTuple, size_t... Indices>
        static constexpr auto flatten_write(std::index_sequence<Indices...>) {
            // clang-format off
            return std::tuple_cat(std::conditional_t<
                is_write_allowed<std::tuple_element_t<Indices, AllTuple>, LockType>::value,
                std::tuple<std::tuple_element_t<Indices, AllTuple> *>,
                std::tuple<>
            >{}...);
            // clang-format on
        }

        template<typename LockType, typename AllTuple>
        static constexpr auto flatten() {
            if constexpr (is_add_remove_allowed<LockType>()) {
                if constexpr (is_read_previous_allowed<LockType>()) {
                    return TransactionPermissions<AddRemove, ReadPrevious>{};
                } else {
                    return TransactionPermissions<AddRemove>{};
                }
            } else {
                using ReadPerm =
                    decltype(flatten_read<LockType, AllTuple>(std::make_index_sequence<std::tuple_size_v<AllTuple>>()));
                using WritePerm = decltype(flatten_write<LockType, AllTuple>(
                    std::make_index_sequence<std::tuple_size_v<AllTuple>>()));
                if constexpr (std::is_same<ReadPerm, std::tuple<>>()) {
                    if constexpr (std::is_same<WritePerm, std::tuple<>>()) {
                        if constexpr (is_read_previous_allowed<LockType>()) {
                            return TransactionPermissions<ReadPrevious>{};
                        } else {
                            return TransactionPermissions<>{};
                        }
                    } else {
                        if constexpr (is_read_previous_allowed<LockType>()) {
                            return TransactionPermissions<typename tuple_to<WritePerm>::write, ReadPrevious>{};
                        } else {
                            return TransactionPermissions<typename tuple_to<WritePerm>::write>{};
                        }
                    }
                } else {
                    if constexpr (std::is_same<WritePerm, std::tuple<>>()) {
                        if constexpr (is_read_previous_allowed<LockType>()) {
                            return TransactionPermissions<typename tuple_to<ReadPerm>::read, ReadPrevious>{};
                        } else {
                            return TransactionPermissions<typename tuple_to<ReadPerm>::read>{};
                        }
                    } else {
                        if constexpr (is_read_previous_allowed<LockType>()) {
                            return TransactionPermissions<typename tuple_to<ReadPerm>::read,
                                typename tuple_to<WritePerm>::write,
                                ReadPrevious>{};
                        } else {
                            return TransactionPermissions<typename tuple_to<ReadPerm>::read,
                                typename tuple_to<WritePerm>::write>{};
                        }
                    }
                }
            }
        }
    } // namespace detail

    template<typename>
    struct FlattenPermissions {};

    template<template<typename, typename...> typename LockType, template<typename...> typename ECSType,
        typename... AllComponentTypes, typename... Permissions>
    struct FlattenPermissions<LockType<ECSType<AllComponentTypes...>, Permissions...>> {
        using AllTuple = std::tuple<AllComponentTypes...>;
        using FullLockType = LockType<ECSType<AllComponentTypes...>, Permissions...>;

        using type = decltype(detail::flatten<FullLockType, AllTuple>());
        using type_readonly = typename detail::tuple_to<decltype(detail::flatten_readonly<FullLockType, AllTuple>(
            std::make_index_sequence<sizeof...(AllComponentTypes)>()))>::read;

        using type_lock = typename detail::tuple_to_lock<type>::type;
        using type_dynamic_lock = typename detail::tuple_to_lock<type>::type_dynamic;
    };
}; // namespace Tecs
