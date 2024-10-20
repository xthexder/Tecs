#pragma once

#include <Tecs.hh>
#include <fstream>
#include <iostream>
#include <memory>
#include <source_location>

#define STRING(s) #s
#define STRINGIFY(s) STRING(s)

#ifdef TECS_C_ABI_ECS_INCLUDE
    #include TECS_C_ABI_ECS_INCLUDE
#endif
#ifndef TECS_C_ABI_ECS_NAME
using ECS = Tecs::ECS<>;
    #define TECS_C_ABI_ECS_NAME ECS
#endif

template<typename T>
auto EmbedTypeIntoSignature() {
    return std::string_view{std::source_location::current().function_name()};
}

template<typename T>
auto TypeToString() {
    auto dummyInt = EmbedTypeIntoSignature<int>();
    auto intStart = dummyInt.find("int");
    auto tailLength = dummyInt.size() - intStart - std::string("int").length();

    auto typeStart = intStart;
    auto embeddingSignature = EmbedTypeIntoSignature<T>();
    auto enumStart = embeddingSignature.find("enum ", intStart);
    if (enumStart == intStart) typeStart += std::string("enum ").length();
    auto classStart = embeddingSignature.find("class ", intStart);
    if (classStart == intStart) typeStart += std::string("class ").length();
    auto structStart = embeddingSignature.find("struct ", intStart);
    if (structStart == intStart) typeStart += std::string("struct ").length();

    auto typeLength = embeddingSignature.size() - typeStart - tailLength;
    return embeddingSignature.substr(typeStart, typeLength);
}

template<typename>
struct CodeGenerator;

template<template<typename...> typename ECSType, typename... AllComponentTypes>
struct CodeGenerator<ECSType<AllComponentTypes...>> {
    static constexpr std::array<std::string_view, sizeof...(AllComponentTypes)> GetComponentNames() {
        return {
            TypeToString<AllComponentTypes>()...,
        };
    }

    static constexpr std::array<bool, sizeof...(AllComponentTypes)> GetComponentGlobalList() {
        return {
            Tecs::is_global_component<AllComponentTypes>()...,
        };
    }
};
