#pragma once

#include "gen_common.hh"

template<typename T>
void generateEntityCC(T &out) {
    auto names = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentNames();
    auto globalList = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentGlobalList();
    auto copyableList = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentCopyableList();
#ifdef TECS_C_ABI_ECS_INCLUDE
    out << "#include " STRINGIFY(TECS_C_ABI_ECS_INCLUDE) << std::endl;
#endif
    out << R"RAWSTR(
#include <Tecs.hh>
#include <c_abi/Tecs_lock.h>

)RAWSTR";
    out << "using ECS = " << TypeToString<TECS_C_ABI_ECS_NAME>();
    out << R"RAWSTR(;
using DynamicLock = Tecs::DynamicLock<ECS>;

extern "C" {

TECS_EXPORT bool Tecs_entity_exists(TecsLock *dynLockPtr, TecsEntity entity) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return Tecs::Entity(entity).Exists(*dynLock);
}

TECS_EXPORT bool Tecs_entity_existed(TecsLock *dynLockPtr, TecsEntity entity) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return Tecs::Entity(entity).Existed(*dynLock);
}

TECS_EXPORT bool Tecs_entity_has(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Error: Entities can't have global components: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return false;" << std::endl;
        } else {
            out << "        return Tecs::Entity(entity).Has<" << names[i] << ">(*dynLock);" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return false;
    }
}

TECS_EXPORT bool Tecs_entity_has_bitset(TecsLock *dynLockPtr, TecsEntity entity, unsigned long long componentBits) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return Tecs::Entity(entity).HasBitset<Tecs::Lock<ECS>>(*dynLock, DynamicLock::PermissionBitset(componentBits));
}

TECS_EXPORT bool Tecs_entity_had(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Error: Entities can't have global components: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return false;" << std::endl;
        } else {
            out << "        return Tecs::Entity(entity).Had<" << names[i] << ">(*dynLock);" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return false;
    }
}

TECS_EXPORT bool Tecs_entity_had_bitset(TecsLock *dynLockPtr, TecsEntity entity, unsigned long long componentBits) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return Tecs::Entity(entity).HadBitset<Tecs::Lock<ECS>>(*dynLock, DynamicLock::PermissionBitset(componentBits));
}

TECS_EXPORT const void *Tecs_const_get_entity_storage(TecsLock *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Error: Entities can't have global components: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else {
            out << "        auto lock = dynLock->TryLock<Tecs::Read<" << names[i] << ">>();" << std::endl;
            out << "        if (!lock) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have " << names[i]
                << " read permissions\" << std::endl;" << std::endl;
            out << "            return nullptr;" << std::endl;
            out << "        }" << std::endl;
            out << "        return lock->GetStorage<const " << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT const void *Tecs_entity_const_get(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex) {
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        out << "        auto *storage = static_cast<const " << names[i]
            << " *>(Tecs_const_get_entity_storage(dynLockPtr, componentIndex));" << std::endl;
        out << "        return &storage[Tecs::Entity(entity).index];" << std::endl;
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT void *Tecs_get_entity_storage(TecsLock *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Error: Entities can't have global components: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else {
            out << "        auto lock1 = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "        if (lock1) {" << std::endl;
            out << "            return lock1->GetStorage<" << names[i] << ">();" << std::endl;
            out << "        }" << std::endl;
            out << "        auto lock2 = dynLock->TryLock<Tecs::Write<" << names[i] << ">>();" << std::endl;
            out << "        if (!lock2) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have " << names[i]
                << " write permissions\" << std::endl;" << std::endl;
            out << "            return nullptr;" << std::endl;
            out << "        }" << std::endl;
            out << "        return lock2->GetStorage<" << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT void *Tecs_entity_get(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex) {
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        out << "        auto *storage = static_cast<" << names[i]
            << " *>(Tecs_get_entity_storage(dynLockPtr, componentIndex));" << std::endl;
        out << "        return &storage[Tecs::Entity(entity).index];" << std::endl;
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT const void *Tecs_get_previous_entity_storage(TecsLock *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Error: Entities can't have global components: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else {
            out << "        auto lock = dynLock->TryLock<Tecs::Read<" << names[i] << ">>();" << std::endl;
            out << "        if (!lock) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have " << names[i]
                << " read permissions\" << std::endl;" << std::endl;
            out << "            return nullptr;" << std::endl;
            out << "        }" << std::endl;
            out << "        return lock->GetPreviousStorage<" << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT const void *Tecs_entity_get_previous(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex) {
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        out << "        auto *storage = static_cast<const " << names[i]
            << " *>(Tecs_get_previous_entity_storage(dynLockPtr, componentIndex));" << std::endl;
        out << "        return &storage[Tecs::Entity(entity).index];" << std::endl;
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT void *Tecs_entity_set(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex, const void *value) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Error: Entities can't have global components: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else if (!copyableList[i]) {
            out << "        std::cerr << \"Error: Can't set component type unless it is trivially copyable: "
                << names[i] << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else {
            out << "        auto lock1 = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "        if (lock1) {" << std::endl;
            out << "            return &Tecs::Entity(entity).Set<" << names[i] << ">(*lock1, *static_cast<const "
                << names[i] << "*>(value));" << std::endl;
            out << "        }" << std::endl;
            out << "        auto lock2 = dynLock->TryLock<Tecs::Write<" << names[i] << ">>();" << std::endl;
            out << "        if (!lock2) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have " << names[i]
                << " write permissions\" << std::endl;" << std::endl;
            out << "            return nullptr;" << std::endl;
            out << "        }" << std::endl;
            out << "        return &Tecs::Entity(entity).Set<" << names[i] << ">(*lock2, *static_cast<const "
                << names[i] << "*>(value));" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT void Tecs_entity_unset(TecsLock *dynLockPtr, TecsEntity entity, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Error: Entities can't have global components: " << names[i]
                << "\" << std::endl;" << std::endl;
        } else {
            out << "        auto lock = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "        if (!lock) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have AddRemove permissions\" << std::endl;"
                << std::endl;
            out << "            return;" << std::endl;
            out << "        }" << std::endl;
            out << "        Tecs::Entity(entity).Unset<" << names[i] << ">(*lock);" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Error: Component index out of range: " << componentIndex << std::endl;
    }
}

TECS_EXPORT void Tecs_entity_destroy(TecsLock *dynLockPtr, TecsEntity entity) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    auto lock = dynLock->TryLock<Tecs::AddRemove>();
    if (!lock) {
        std::cerr << "Error: Lock does not have AddRemove permissions" << std::endl;
    } else {
        Tecs::Entity(entity).Destroy(*lock);
    }
}
}
)RAWSTR";
}
