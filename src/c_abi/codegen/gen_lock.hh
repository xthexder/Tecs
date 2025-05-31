#pragma once

#include "gen_common.hh"

template<typename S>
void generateLockH(S &out) {
    out << R"RAWSTR(#pragma once

#include "c_abi/Tecs_entity.h"
#include "c_abi/Tecs_entity_view.h"
#include "c_abi/Tecs_export.h"

#ifdef __cplusplus
extern "C" {
#else
    #include <stdint.h>

typedef uint8_t bool;
#endif

#include <stddef.h>
)RAWSTR";
#ifdef TECS_C_ABI_ECS_C_INCLUDE
    out << "#include " STRINGIFY(TECS_C_ABI_ECS_C_INCLUDE) << std::endl;
#endif
    out << R"RAWSTR(

typedef void tecs_lock_t;
)RAWSTR";
    auto snakeCaseNames = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentSnakeCaseNames();
    auto globalList = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentGlobalList();
    for (size_t i = 0; i < snakeCaseNames.size(); i++) {
        auto &scn = snakeCaseNames[i];
        out << std::endl;
        out << "TECS_EXPORT bool Tecs_lock_is_write_" << scn << "_allowed(tecs_lock_t *dynLockPtr);" << std::endl;
        out << "TECS_EXPORT bool Tecs_lock_is_read_" << scn << "_allowed(tecs_lock_t *dynLockPtr);" << std::endl;

        out << std::endl;
        if (!globalList[i]) {

            out << "TECS_EXPORT size_t Tecs_previous_entities_with_" << scn
                << "(tecs_lock_t *dynLockPtr, tecs_entity_view_t *output);" << std::endl;
            out << "TECS_EXPORT size_t Tecs_entities_with_" << scn
                << "(tecs_lock_t *dynLockPtr, tecs_entity_view_t *output);" << std::endl;
        } else {
            out << "TECS_EXPORT bool Tecs_has_" << scn << "(tecs_lock_t *dynLockPtr);" << std::endl;
            out << "TECS_EXPORT bool Tecs_had_" << scn << "(tecs_lock_t *dynLockPtr);" << std::endl;
            out << "TECS_EXPORT const void *Tecs_const_get_" << scn << "(tecs_lock_t *dynLockPtr);" << std::endl;
            out << "TECS_EXPORT void *Tecs_get_" << scn << "(tecs_lock_t *dynLockPtr);" << std::endl;
            out << "TECS_EXPORT const void *Tecs_get_previous_" << scn << "(tecs_lock_t *dynLockPtr);" << std::endl;
            out << "TECS_EXPORT void *Tecs_set_" << scn << "(tecs_lock_t *dynLockPtr, const void *value);" << std::endl;
            out << "TECS_EXPORT void Tecs_unset_" << scn << "(tecs_lock_t *dynLockPtr);" << std::endl;
        }
    }
    out << R"RAWSTR(
#ifdef __cplusplus
}
#endif
)RAWSTR";
}

template<typename S>
void generateLockCC(S &out) {
    auto names = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentNames();
    auto snakeCaseNames = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentSnakeCaseNames();
    auto globalList = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentGlobalList();
#ifdef TECS_C_ABI_ECS_INCLUDE
    out << "#include " STRINGIFY(TECS_C_ABI_ECS_INCLUDE) << std::endl;
#endif
#ifdef TECS_C_ABI_ECS_C_INCLUDE
    out << "#include " STRINGIFY(TECS_C_ABI_ECS_C_INCLUDE) << std::endl;
#endif
    out << R"RAWSTR(
#include <Tecs.hh>
#include <c_abi/Tecs_lock.h>

)RAWSTR";
    out << "using ECS = " << TypeToString<TECS_C_ABI_ECS_NAME>();
    out << R"RAWSTR(;
using DynamicLock = Tecs::DynamicLock<ECS>;

extern "C" {

TECS_EXPORT size_t Tecs_lock_get_transaction_id(tecs_lock_t *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return dynLock->GetTransactionId();
}

TECS_EXPORT bool Tecs_lock_is_add_remove_allowed(tecs_lock_t *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return dynLock->TryLock<Tecs::AddRemove>().has_value();
}

TECS_EXPORT bool Tecs_lock_is_write_allowed(tecs_lock_t *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        out << "         return dynLock->TryLock<Tecs::Write<" << names[i] << ">>().has_value();" << std::endl;
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return false;
    }
}

TECS_EXPORT bool Tecs_lock_is_read_allowed(tecs_lock_t *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        out << "         return dynLock->TryLock<Tecs::Read<" << names[i] << ">>().has_value();" << std::endl;
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return false;
    }
}

TECS_EXPORT size_t Tecs_previous_entities_with(tecs_lock_t *dynLockPtr, size_t componentIndex, tecs_entity_view_t *output) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    Tecs::EntityView view;
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Entities can't have global components: " << names[i] << "\" << std::endl;"
                << std::endl;
            out << "        return 0;" << std::endl;
        } else {
            out << "        view = dynLock->PreviousEntitiesWith<" << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return 0;
    }
    *output = tecs_entity_view_t {
        .storage = view.storage,
        .start_index = view.start_index,
        .end_index = view.end_index,
    };
    return view.size();
}

TECS_EXPORT size_t Tecs_entities_with(tecs_lock_t *dynLockPtr, size_t componentIndex, tecs_entity_view_t *output) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    Tecs::EntityView view;
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (globalList[i]) {
            out << "        std::cerr << \"Entities can't have global components: " << names[i] << "\" << std::endl;"
                << std::endl;
            out << "        return 0;" << std::endl;
        } else {
            out << "        view = dynLock->EntitiesWith<" << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return 0;
    }
    *output = tecs_entity_view_t {
        .storage = view.storage,
        .start_index = view.start_index,
        .end_index = view.end_index,
    };
    return view.size();
}

TECS_EXPORT size_t Tecs_previous_entities(tecs_lock_t *dynLockPtr, tecs_entity_view_t *output) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    auto view = dynLock->PreviousEntities();
    *output = tecs_entity_view_t {
        .storage = view.storage,
        .start_index = view.start_index,
        .end_index = view.end_index,
    };
    return view.size();
}

TECS_EXPORT size_t Tecs_entities(tecs_lock_t *dynLockPtr, tecs_entity_view_t *output) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    auto view = dynLock->Entities();
    *output = tecs_entity_view_t {
        .storage = view.storage,
        .start_index = view.start_index,
        .end_index = view.end_index,
    };
    return view.size();
}

TECS_EXPORT tecs_entity_t Tecs_new_entity(tecs_lock_t *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    auto lock = dynLock->TryLock<Tecs::AddRemove>();
    if (!lock) {
        std::cerr << "Error: Lock does not have AddRemove permissions" << std::endl;
        return 0;
    }
    return (size_t)lock->NewEntity();
}

TECS_EXPORT bool Tecs_has(tecs_lock_t *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (!globalList[i]) {
            out << "        std::cerr << \"Only global components can be accessed without an Entity: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return false;" << std::endl;
        } else {
            out << "        return dynLock->Has<" << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return false;
    }
}

TECS_EXPORT bool Tecs_had(tecs_lock_t *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (!globalList[i]) {
            out << "        std::cerr << \"Only global components can be accessed without an Entity: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return false;" << std::endl;
        } else {
            out << "        return dynLock->Had<" << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return false;
    }
}

TECS_EXPORT const void *Tecs_const_get(tecs_lock_t *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (!globalList[i]) {
            out << "        std::cerr << \"Only global components can be accessed without an Entity: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else {
            out << "        auto lock = dynLock->TryLock<Tecs::Read<" << names[i] << ">>();" << std::endl;
            out << "        if (!lock) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have " << names[i]
                << " read permissions\" << std::endl;" << std::endl;
            out << "            return nullptr;" << std::endl;
            out << "        }" << std::endl;
            out << "        return &lock->Get<const " << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT void *Tecs_get(tecs_lock_t *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (!globalList[i]) {
            out << "        std::cerr << \"Only global components can be accessed without an Entity: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else {
            out << "        auto lock1 = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "        if (lock1) {" << std::endl;
            out << "            return &lock1->Get<" << names[i] << ">();" << std::endl;
            out << "        }" << std::endl;
            out << "        auto lock2 = dynLock->TryLock<Tecs::Write<" << names[i] << ">>();" << std::endl;
            out << "        if (!lock2) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have " << names[i]
                << " write permissions\" << std::endl;" << std::endl;
            out << "            return nullptr;" << std::endl;
            out << "        }" << std::endl;
            out << "        return &lock2->Get<" << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT const void *Tecs_get_previous(tecs_lock_t *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (!globalList[i]) {
            out << "        std::cerr << \"Only global components can be accessed without an Entity: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else {
            out << "        auto lock = dynLock->TryLock<Tecs::Read<" << names[i] << ">>();" << std::endl;
            out << "        if (!lock) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have " << names[i]
                << " read permissions\" << std::endl;" << std::endl;
            out << "            return nullptr;" << std::endl;
            out << "        }" << std::endl;
            out << "        return &lock->GetPrevious<const " << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT void *Tecs_set(tecs_lock_t *dynLockPtr, size_t componentIndex, const void *value) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (!globalList[i]) {
            out << "        std::cerr << \"Only global components can be accessed without an Entity: " << names[i]
                << "\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
        } else {
            out << "        auto lock1 = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "        if (lock1) {" << std::endl;
            out << "            return &lock1->Set<" << names[i] << ">(*static_cast<const " << names[i] << "*>(value));"
                << std::endl;
            out << "        }" << std::endl;
            out << "        auto lock2 = dynLock->TryLock<Tecs::Write<" << names[i] << ">>();" << std::endl;
            out << "        if (!lock2) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have " << names[i]
                << " write permissions\" << std::endl;" << std::endl;
            out << "            return nullptr;" << std::endl;
            out << "        }" << std::endl;
            out << "        return &lock2->Set<" << names[i] << ">(*static_cast<const " << names[i] << "*>(value));"
                << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
        return nullptr;
    }
}

TECS_EXPORT void Tecs_unset(tecs_lock_t *dynLockPtr, size_t componentIndex) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    // For each component...
)RAWSTR";
    for (size_t i = 0; i < names.size(); i++) {
        if (i == 0) {
            out << "    if (componentIndex == 0) {" << std::endl;
        } else {
            out << "    } else if (componentIndex == " << i << ") {" << std::endl;
        }
        if (!globalList[i]) {
            out << "        std::cerr << \"Only global components can be accessed without an Entity: " << names[i]
                << "\" << std::endl;" << std::endl;
        } else {
            out << "        auto lock = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "        if (!lock) {" << std::endl;
            out << "            std::cerr << \"Error: Lock does not have AddRemove permissions\" << std::endl;"
                << std::endl;
            out << "            return;" << std::endl;
            out << "        }" << std::endl;
            out << "        lock->Unset<" << names[i] << ">();" << std::endl;
        }
    }
    out << "    } else {";
    out << R"RAWSTR(
        std::cerr << "Component index out of range: " << componentIndex << std::endl;
    }
}
)RAWSTR";
    for (size_t i = 0; i < snakeCaseNames.size(); i++) {
        auto &scn = snakeCaseNames[i];
        out << std::endl;
        out << "TECS_EXPORT bool Tecs_lock_is_write_" << scn << "_allowed(tecs_lock_t *dynLockPtr) {" << std::endl;
        out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
        out << "    return dynLock->TryLock<Tecs::Write<" << names[i] << ">>().has_value();" << std::endl;
        out << "}" << std::endl;
        out << std::endl;
        out << "TECS_EXPORT bool Tecs_lock_is_read_" << scn << "_allowed(tecs_lock_t *dynLockPtr) {" << std::endl;
        out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
        out << "    return dynLock->TryLock<Tecs::Read<" << names[i] << ">>().has_value();" << std::endl;
        out << "}" << std::endl;
        out << std::endl;

        if (!globalList[i]) {
            out << "TECS_EXPORT size_t Tecs_previous_entities_with_" << scn
                << "(tecs_lock_t *dynLockPtr, tecs_entity_view_t *output) {" << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    Tecs::EntityView view = dynLock->PreviousEntitiesWith<" << names[i] << ">();" << std::endl;
            out << "    *output = tecs_entity_view_t{" << std::endl;
            out << "        .storage = view.storage," << std::endl;
            out << "        .start_index = view.start_index," << std::endl;
            out << "        .end_index = view.end_index," << std::endl;
            out << "    };" << std::endl;
            out << "    return view.size();" << std::endl;
            out << "}" << std::endl;
            out << std::endl;
            out << "TECS_EXPORT size_t Tecs_entities_with_" << scn
                << "(tecs_lock_t *dynLockPtr, tecs_entity_view_t *output) {" << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    Tecs::EntityView view = dynLock->EntitiesWith<" << names[i] << ">();" << std::endl;
            out << "    *output = tecs_entity_view_t{" << std::endl;
            out << "        .storage = view.storage," << std::endl;
            out << "        .start_index = view.start_index," << std::endl;
            out << "        .end_index = view.end_index," << std::endl;
            out << "    };" << std::endl;
            out << "    return view.size();" << std::endl;
            out << "}" << std::endl;
        } else {
            out << "TECS_EXPORT bool Tecs_has_" << scn << "(tecs_lock_t *dynLockPtr) {" << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    return dynLock->Has<" << names[i] << ">();" << std::endl;
            out << "}" << std::endl;
            out << std::endl;
            out << "TECS_EXPORT bool Tecs_had_" << scn << "(tecs_lock_t *dynLockPtr) {" << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    return dynLock->Had<" << names[i] << ">();" << std::endl;
            out << "}" << std::endl;
            out << std::endl;
            out << "TECS_EXPORT const void *Tecs_const_get_" << scn << "(tecs_lock_t *dynLockPtr) {" << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    auto lock = dynLock->TryLock<Tecs::Read<" << names[i] << ">>();" << std::endl;
            out << "    if (!lock) {" << std::endl;
            out << "        std::cerr << \"Error: Lock does not have " << names[i]
                << " read permissions\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
            out << "    }" << std::endl;
            out << "    return &lock->Get<const " << names[i] << ">();" << std::endl;
            out << "}" << std::endl;
            out << std::endl;
            out << "TECS_EXPORT void *Tecs_get_" << scn << "(tecs_lock_t *dynLockPtr) {" << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    auto lock1 = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "    if (lock1) {" << std::endl;
            out << "        return &lock1->Get<" << names[i] << ">();" << std::endl;
            out << "    }" << std::endl;
            out << "    auto lock2 = dynLock->TryLock<Tecs::Write<" << names[i] << ">>();" << std::endl;
            out << "    if (!lock2) {" << std::endl;
            out << "        std::cerr << \"Error: Lock does not have " << names[i]
                << " write permissions\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
            out << "    }" << std::endl;
            out << "    return &lock2->Get<" << names[i] << ">();" << std::endl;
            out << "}" << std::endl;
            out << std::endl;
            out << "TECS_EXPORT const void *Tecs_get_previous_" << scn << "(tecs_lock_t *dynLockPtr) {" << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    auto lock = dynLock->TryLock<Tecs::Read<" << names[i] << ">>();" << std::endl;
            out << "    if (!lock) {" << std::endl;
            out << "        std::cerr << \"Error: Lock does not have " << names[i]
                << " read permissions\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
            out << "    }" << std::endl;
            out << "    return &lock->GetPrevious<const " << names[i] << ">();" << std::endl;
            out << "}" << std::endl;
            out << std::endl;
            out << "TECS_EXPORT void *Tecs_set_" << scn << "(tecs_lock_t *dynLockPtr, const void *value) {"
                << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    auto lock1 = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "    if (lock1) {" << std::endl;
            out << "        return &lock1->Set<" << names[i] << ">(*static_cast<const " << names[i] << "*>(value));"
                << std::endl;
            out << "    }" << std::endl;
            out << "    auto lock2 = dynLock->TryLock<Tecs::Write<" << names[i] << ">>();" << std::endl;
            out << "    if (!lock2) {" << std::endl;
            out << "        std::cerr << \"Error: Lock does not have " << names[i]
                << " write permissions\" << std::endl;" << std::endl;
            out << "        return nullptr;" << std::endl;
            out << "    }" << std::endl;
            out << "    return &lock2->Set<" << names[i] << ">(*static_cast<const " << names[i] << "*>(value));"
                << std::endl;
            out << "}" << std::endl;
            out << std::endl;
            out << "TECS_EXPORT void Tecs_unset_" << scn << "(tecs_lock_t *dynLockPtr) {" << std::endl;
            out << "    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);" << std::endl;
            out << "    auto lock = dynLock->TryLock<Tecs::AddRemove>();" << std::endl;
            out << "    if (!lock) {" << std::endl;
            out << "        std::cerr << \"Error: Lock does not have AddRemove permissions\" << std::endl;"
                << std::endl;
            out << "        return;" << std::endl;
            out << "    }" << std::endl;
            out << "    lock->Unset<" << names[i] << ">();" << std::endl;
            out << "}" << std::endl;
        }
    }
    out << R"RAWSTR(
// Observer<Event> Watch();
// void StopWatching(Observer<Event> observer);

TECS_EXPORT tecs_lock_t *Tecs_lock_read_only(tecs_lock_t *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return new DynamicLock(dynLock->ReadOnlySubset());
}

} // extern "C"
)RAWSTR";
}
