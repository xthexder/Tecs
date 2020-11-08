#include "test_components.hh"

#include "test_ecs.hh"

#include <Tecs.hh>
#include <cstdint>
#include <string>
#include <vector>

namespace testing {
    bool Transform::HasParent(Tecs::ReadLock<ECS, Transform> lock) {
        Tecs::ReadLock<ECS, Transform> readLock = lock;
        for (auto entity : readLock.ValidEntities<Transform>()) {
            if (entity == parent) { return true; }
        }
        return false;
    }
}; // namespace testing
