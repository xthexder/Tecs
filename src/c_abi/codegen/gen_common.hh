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
#ifndef TECS_C_ABI_TYPE_PREFIX
    #define TECS_C_ABI_TYPE_PREFIX ""
#endif

template<typename T>
auto EmbedTypeIntoSignature() {
    const char *funcName = std::source_location::current().function_name();
    return std::string_view(std::strcmp("EmbedTypeIntoSignature", funcName) == 0 ? typeid(T).name() : funcName);
}

template<typename T>
auto TypeToString() {
    auto dummyChar = EmbedTypeIntoSignature<unsigned char>();
    auto charStart = dummyChar.find("unsigned char");
    auto tailLength = dummyChar.size() - charStart - std::string("unsigned char").size();

    auto typeStart = charStart;
    auto embeddingSignature = EmbedTypeIntoSignature<T>();
    auto enumStart = embeddingSignature.find("enum ", charStart);
    if (enumStart == charStart) typeStart += std::string("enum ").length();
    auto classStart = embeddingSignature.find("class ", charStart);
    if (classStart == charStart) typeStart += std::string("class ").length();
    auto structStart = embeddingSignature.find("struct ", charStart);
    if (structStart == charStart) typeStart += std::string("struct ").length();

    auto typeLength = embeddingSignature.size() - typeStart - tailLength;
    return embeddingSignature.substr(typeStart, typeLength);
}

std::string SnakeCaseTypeName(std::string_view name) {
    std::string snakeCaseName;
    bool wasCaps = true;
    bool wasSep = false;
    for (const char &ch : name) {
        if (ch != std::tolower(ch)) {
            if (!wasCaps) snakeCaseName.append(1, '_');
            wasCaps = true;
        } else {
            wasCaps = false;
        }
        if (ch == ':') {
            if (!wasSep) snakeCaseName.append(1, '_');
            wasSep = true;
            wasCaps = true;
        } else {
            wasSep = false;
        }
        snakeCaseName.append(1, (char)std::tolower(ch));
    }
    return snakeCaseName;
}

template<typename ECS, typename T>
std::string TypeToCName() {
    if constexpr (std::is_enum<T>()) {
        if constexpr (sizeof(T) == sizeof(int)) {
            return std::string("enum ") + TECS_C_ABI_TYPE_PREFIX +
                   SnakeCaseTypeName(ECS::template GetComponentName<T>()) + "_t";
        } else {
            return std::string(TypeToString<std::underlying_type_t<T>>());
        }
    } else {
        return TECS_C_ABI_TYPE_PREFIX + SnakeCaseTypeName(ECS::template GetComponentName<T>()) + "_t";
    }
}

template<typename>
struct CodeGenerator;

template<template<typename...> typename ECSType, typename... AllComponentTypes>
struct CodeGenerator<ECSType<AllComponentTypes...>> {
    using ECS = ECSType<AllComponentTypes...>;

    static constexpr std::array<std::string_view, sizeof...(AllComponentTypes)> GetComponentNames() {
        return {
            TypeToString<AllComponentTypes>()...,
        };
    }

    static constexpr std::array<std::string, sizeof...(AllComponentTypes)> GetComponentSnakeCaseNames() {
        return {
            SnakeCaseTypeName(ECS::template GetComponentName<AllComponentTypes>())...,
        };
    }

    static constexpr std::array<std::string, sizeof...(AllComponentTypes)> GetComponentCTypeName() {
        return {
            TypeToCName<ECS, AllComponentTypes>()...,
        };
    }

    static constexpr std::array<bool, sizeof...(AllComponentTypes)> GetComponentGlobalList() {
        return {
            Tecs::is_global_component<AllComponentTypes>()...,
        };
    }

    static constexpr std::array<bool, sizeof...(AllComponentTypes)> GetComponentCopyableList() {
        return {
            std::is_copy_constructible<AllComponentTypes>()...,
        };
    }
};
