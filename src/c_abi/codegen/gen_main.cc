#include "gen_ecs.hh"
#include "gen_entity.hh"
#include "gen_lock.hh"

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: codegen out/ecs.h out/ecs.cc" << std::endl;
        return -1;
    }
    {
        auto out = std::ofstream(argv[1], std::ios::trunc);
        out << R"RAWSTR(/*
 * THIS FILE IS AUTO-GENERATED -- DO NOT EDIT
 * See src/c_abi/codegen/gen_main.cc to modify
 */

#pragma once

#include "c_abi/Tecs_entity.h"
#include "c_abi/Tecs_entity_view.h"
#include "c_abi/Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(bool) == 1, "Unexpected bool size");
)RAWSTR";
#ifdef TECS_C_ABI_ECS_INCLUDE
        out << "#include " STRINGIFY(TECS_C_ABI_ECS_INCLUDE) << std::endl;
#endif
        out << R"RAWSTR(
typedef void tecs_lock_t;
typedef uint64_t tecs_entity_t;
)RAWSTR";
        generateLockH(out);
        generateEntityH(out);
        out << R"RAWSTR(
#ifdef __cplusplus
}
#endif
)RAWSTR";
    }
    {
        auto out = std::ofstream(argv[2], std::ios::trunc);
        out << R"RAWSTR(/*
 * THIS FILE IS AUTO-GENERATED -- DO NOT EDIT
 * See src/c_abi/codegen/gen_main.cc to modify
 */

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
    #define _CRT_SECURE_NO_WARNINGS
#endif

)RAWSTR";
#ifdef TECS_C_ABI_ECS_IMPL_INCLUDE
        out << "#include " STRINGIFY(TECS_C_ABI_ECS_IMPL_INCLUDE) << std::endl;
#elif defined(TECS_C_ABI_ECS_INCLUDE)
        out << "#include " STRINGIFY(TECS_C_ABI_ECS_INCLUDE) << std::endl;
#endif
        out << R"RAWSTR(
#include <Tecs.hh>
#include <c_abi/Tecs.h>
#include <c_abi/Tecs_lock.h>
#include <cstring>

)RAWSTR";
        out << "using ECS = " << TypeToString<TECS_C_ABI_ECS_NAME>();
        out << R"RAWSTR(;
using DynamicLock = Tecs::DynamicLock<ECS>;

extern "C" {
)RAWSTR";
        generateECSCC(out);
        generateEntityCC(out);
        generateLockCC(out);
        out << R"RAWSTR(
} // extern "C"
)RAWSTR";
    }
}
