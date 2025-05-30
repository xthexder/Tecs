#pragma once

#include "gen_common.hh"

template<typename T>
void generateECSCC(T &out) {
    out << "#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)" << std::endl;
    out << "    #define _CRT_SECURE_NO_WARNINGS" << std::endl;
    out << "#endif" << std::endl;
    auto names = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentNames();
#ifdef TECS_C_ABI_ECS_INCLUDE
    out << std::endl << "#include " STRINGIFY(TECS_C_ABI_ECS_INCLUDE);
#endif
    out << R"RAWSTR(
#include <Tecs.hh>
#include <c_abi/Tecs.h>
#include <cstring>

)RAWSTR";
    out << "using ECS = " << TypeToString<TECS_C_ABI_ECS_NAME>();
    out << R"RAWSTR(;
using DynamicLock = Tecs::DynamicLock<ECS>;

extern "C" {

TECS_EXPORT TecsECS *Tecs_make_ecs_instance() {
    return new ECS();
}

TECS_EXPORT void Tecs_release_ecs_instance(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    delete ecs;
}

TECS_EXPORT TecsLock *Tecs_ecs_start_transaction(TecsECS *ecsPtr, uint64_t readPermissions, uint64_t writePermissions) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    if constexpr (1 + ECS::GetComponentCount() > std::numeric_limits<uint64_t>::digits) {
        std::cerr << "Too many components to use uint64 init: " << ECS::GetComponentCount() << std::endl;
        return nullptr;
    } else {
        return new DynamicLock(*ecs,
            DynamicLock::PermissionBitset(readPermissions),
            DynamicLock::PermissionBitset(writePermissions));
    }
}

TECS_EXPORT TecsLock *Tecs_ecs_start_transaction_bitstr(TecsECS *ecsPtr, const char *readPermissions,
    const char *writePermissions) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return new DynamicLock(*ecs,
        DynamicLock::PermissionBitset(std::string(readPermissions)),
        DynamicLock::PermissionBitset(std::string(writePermissions)));
}

TECS_EXPORT size_t Tecs_ecs_get_instance_id(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return ecs->GetInstanceId();
}

TECS_EXPORT size_t Tecs_ecs_get_next_transaction_id(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return ecs->GetNextTransactionId();
}

TECS_EXPORT size_t Tecs_ecs_get_component_count(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return ecs->GetComponentCount();
}

TECS_EXPORT size_t Tecs_ecs_get_component_name(TecsECS *ecsPtr, size_t componentIndex, size_t bufferSize, char *output) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    std::string name;
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        out << "        name = ecs->GetComponentName<" << names[i] << ">();" << std::endl;
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return 0;
    }
    if (name.size() < bufferSize) {
        (void)std::strncpy(output, name.c_str(), bufferSize);
    }
    return name.size() + 1;
}

TECS_EXPORT size_t Tecs_ecs_get_bytes_per_entity(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return ecs->GetBytesPerEntity();
}

TECS_EXPORT void Tecs_lock_release(TecsLock *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    delete dynLock;
}

} // extern "C"
)RAWSTR";
}
