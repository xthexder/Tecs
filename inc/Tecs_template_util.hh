#pragma once

#include <tuple>
#include <type_traits>

namespace Tecs {
    // contains<T, Un...>::value is true if T is part of the set Un...
    template<typename T, typename... Un>
    struct contains : std::disjunction<std::is_same<T, Un>...> {};

    // is_subset_of<std::tuple<Tn...>, std::tuple<Un...>>::value is true if Tn... is a subset of Un...
    template<typename...>
    struct is_subset_of : std::false_type {};

    template<typename... Tn, typename... Un>
    struct is_subset_of<std::tuple<Tn...>, std::tuple<Un...>> : std::conjunction<contains<Tn, Un...>...> {};

    // Template magic to create
    //     std::tuple<ComponentIndex<T1>, ComponentIndex<T2>, ...>
    // from
    //     wrap_tuple_args<ComponentIndex, T1, T2, ...>::type
    template<template<typename> typename W, typename... Tn>
    struct wrap_tuple_args;

    template<template<typename> typename W, typename Tnew>
    struct wrap_tuple_args<W, Tnew> {
        using type = std::tuple<W<Tnew>>;
    };

    template<template<typename> typename W, typename... Texisting, typename Tnew>
    struct wrap_tuple_args<W, std::tuple<Texisting...>, Tnew> {
        using type = std::tuple<Texisting..., W<Tnew>>;
    };

    template<template<typename> typename W, typename Tnew, typename... Tremaining>
    struct wrap_tuple_args<W, Tnew, Tremaining...> {
        using type = typename wrap_tuple_args<W, std::tuple<W<Tnew>>, Tremaining...>::type;
    };

    template<template<typename> typename W, typename... Texisting, typename Tnew, typename... Tremaining>
    struct wrap_tuple_args<W, std::tuple<Texisting...>, Tnew, Tremaining...> {
        using type = typename wrap_tuple_args<W, std::tuple<Texisting..., W<Tnew>>, Tremaining...>::type;
    };
}; // namespace Tecs
