#include "impl_gen_common.hh"

template<typename T>
void generateECSCC(T &out) {
    auto names = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentNames();
#ifdef TECS_C_ABI_ECS_INCLUDE
    out << "#include " STRINGIFY(TECS_C_ABI_ECS_INCLUDE) << std::endl;
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

TecsLock *Tecs_ecs_start_transaction(TecsECS *ecsPtr, unsigned long long readPermissions,
    unsigned long long writePermissions) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    if constexpr (1 + ECS::GetComponentCount() > std::numeric_limits<unsigned long long>::digits) {
        std::cerr << "Too many components to use uint64 init: " << ECS::GetComponentCount() << std::endl;
        return nullptr;
    } else {
        return new DynamicLock(*ecs,
            DynamicLock::PermissionBitset(readPermissions),
            DynamicLock::PermissionBitset(writePermissions));
    }
}

TecsLock *Tecs_ecs_start_transaction_bitstr(TecsECS *ecsPtr, const char *readPermissions,
    const char *writePermissions) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return new DynamicLock(*ecs,
        DynamicLock::PermissionBitset(std::string(readPermissions)),
        DynamicLock::PermissionBitset(std::string(writePermissions)));
}

size_t Tecs_ecs_get_instance_id(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return ecs->GetInstanceId();
}

size_t Tecs_ecs_get_component_count(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return ecs->GetComponentCount();
}

size_t Tecs_ecs_get_component_name(TecsECS *ecsPtr, size_t componentIndex, size_t bufferSize, char *output) {
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
        (void)strcpy_s(output, bufferSize, name.c_str());
    }
    return name.size() + 1;
}

size_t Tecs_ecs_get_bytes_per_entity(TecsECS *ecsPtr) {
    ECS *ecs = static_cast<ECS *>(ecsPtr);
    return ecs->GetBytesPerEntity();
}

void Tecs_lock_release(TecsLock *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    delete dynLock;
}

} // extern "C"
)RAWSTR";
}

int main(int argc, char **argv) {
    if (argc > 1) {
        auto out = std::ofstream(argv[1], std::ios::trunc);
        generateECSCC(out);
    } else {
        generateECSCC(std::cout);
    }
}
