#pragma once

#include "test_ecs.hh"

#include <Tecs.hh>

namespace testing {
    struct Transform {
        double pos[3] = {0};
        Tecs::Entity parent;

        Transform() {}
        Transform(double x, double y, double z) : parent() {
            pos[0] = x;
            pos[1] = y;
            pos[2] = z;
        }
        Transform(double x, double y, double z, Tecs::Entity parent) : parent(parent) {
            pos[0] = x;
            pos[1] = y;
            pos[2] = z;
        }

        bool HasParent(Tecs::Lock<ECS, Tecs::Read<Transform>> lock);

        inline bool operator==(const Transform &other) const {
            return pos[0] == other.pos[0] && pos[1] == other.pos[1] && pos[2] == other.pos[2] && parent == other.parent;
        }
    };
} // namespace testing
