#pragma once

#include <tuple>
#include <type_traits>

namespace Tecs {
	// is_type_in_set<T, Un...>::value is true if T is in the set Un
	template<typename T, typename... Un>
	struct is_type_in_set {};

	template<typename T, typename U>
	struct is_type_in_set<T, U> {
		static constexpr bool value = std::is_same<T, U>::value;
	};

	template<typename T, typename U, typename... Un>
	struct is_type_in_set<T, U, Un...> {
		static constexpr bool value = is_type_in_set<T, U>::value || is_type_in_set<T, Un...>::value;
	};

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
