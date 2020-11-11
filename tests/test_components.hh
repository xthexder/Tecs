#pragma once

#include "test_ecs.hh"

#include <Tecs.hh>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

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
    };

    struct Script {
        std::vector<uint8_t> data;

        Script() {}
        Script(uint8_t *data, size_t size) : data(data, data + size) {}
        Script(std::initializer_list<uint8_t> init) : data(init) {}
    };

    struct Renderable {
        std::string name;

        Renderable() {}
        Renderable(std::string name) : name(name) {}
    };
}; // namespace testing
