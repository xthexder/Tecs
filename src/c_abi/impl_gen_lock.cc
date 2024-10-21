#include "impl_gen_common.hh"

template<typename T>
void generateLockCC(T &out) {
    auto names = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentNames();
    auto globalList = CodeGenerator<TECS_C_ABI_ECS_NAME>::GetComponentGlobalList();
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

size_t Tecs_lock_get_transaction_id(TecsLock *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return dynLock->GetTransactionId();
}

bool Tecs_lock_is_add_remove_allowed(TecsLock *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return dynLock->TryLock<Tecs::AddRemove>().has_value();
}

bool Tecs_lock_is_write_allowed(TecsLock *dynLockPtr, size_t componentIndex) {
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

bool Tecs_lock_is_read_allowed(TecsLock *dynLockPtr, size_t componentIndex) {
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

size_t Tecs_previous_entities_with(TecsLock *dynLockPtr, size_t componentIndex, TecsEntityView *output) {
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
    *output = TecsEntityView {
        .storage = view.storage,
        .start_index = view.start_index,
        .end_index = view.end_index,
    };
    return view.size();
}

size_t Tecs_entities_with(TecsLock *dynLockPtr, size_t componentIndex, TecsEntityView *output) {
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
    *output = TecsEntityView {
        .storage = view.storage,
        .start_index = view.start_index,
        .end_index = view.end_index,
    };
    return view.size();
}

size_t Tecs_previous_entities(TecsLock *dynLockPtr, TecsEntityView *output) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    auto view = dynLock->PreviousEntities();
    *output = TecsEntityView {
        .storage = view.storage,
        .start_index = view.start_index,
        .end_index = view.end_index,
    };
    return view.size();
}

size_t Tecs_entities(TecsLock *dynLockPtr, TecsEntityView *output) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    auto view = dynLock->Entities();
    *output = TecsEntityView {
        .storage = view.storage,
        .start_index = view.start_index,
        .end_index = view.end_index,
    };
    return view.size();
}

TecsEntity Tecs_new_entity(TecsLock *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    auto lock = dynLock->TryLock<Tecs::AddRemove>();
    if (!lock) {
        std::cerr << "Error: Lock does not have AddRemove permissions" << std::endl;
        return 0;
    }
    return (size_t)lock->NewEntity();
}

bool Tecs_has(TecsLock *dynLockPtr, size_t componentIndex) {
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

bool Tecs_had(TecsLock *dynLockPtr, size_t componentIndex) {
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

const void *Tecs_const_get(TecsLock *dynLockPtr, size_t componentIndex) {
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

void *Tecs_get(TecsLock *dynLockPtr, size_t componentIndex) {
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

const void *Tecs_get_previous(TecsLock *dynLockPtr, size_t componentIndex) {
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

void *Tecs_set(TecsLock *dynLockPtr, size_t componentIndex, const void *value) {
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

void Tecs_unset(TecsLock *dynLockPtr, size_t componentIndex) {
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

// Observer<Event> Watch();
// void StopWatching(Observer<Event> observer);

TecsLock *Tecs_lock_read_only(TecsLock *dynLockPtr) {
    DynamicLock *dynLock = static_cast<DynamicLock *>(dynLockPtr);
    return new DynamicLock(dynLock->ReadOnlySubset());
}

} // extern "C"
)RAWSTR";
}

int main(int argc, char **argv) {
    if (argc > 1) {
        auto out = std::ofstream(argv[1], std::ios::trunc);
        generateLockCC(out);
    } else {
        generateLockCC(std::cout);
    }
}
