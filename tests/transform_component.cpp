#include "transform_component.hh"

// Intentionally leaving out test_components.hh include to test ECS access with only partial component definitions.

namespace testing {
    bool Transform::HasParent(Tecs::Lock<ECS, Tecs::Read<Transform>> lock) {
        Tecs::Lock<ECS, Tecs::Read<Transform>> readLock = lock;
        for (auto entity : readLock.EntitiesWith<Transform>()) {
            if (entity == parent) { return true; }
        }
        return false;
    }
}; // namespace testing
