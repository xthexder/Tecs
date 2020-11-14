#include "test_components.hh"

#include "test_ecs.hh"

#include <Tecs.hh>
#include <cstdint>
#include <string>
#include <vector>

namespace testing {
    bool Transform::HasParent(Tecs::Lock<ECS, Tecs::Read<Transform>> lock) {
        Tecs::Lock<ECS, Tecs::Read<Transform>> readLock = lock;
        for (auto entity : readLock.EntitiesWith<Transform>()) {
            if (entity == parent) { return true; }
        }
        return false;
    }
}; // namespace testing
