#pragma once

#include "test_ecs.hh"

#include <Tecs.hh>
#include <cstdint>
#include <memory>
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

        inline bool operator==(const Transform &other) {
            return pos[0] == other.pos[0] && pos[1] == other.pos[1] && pos[2] == other.pos[2] && parent == other.parent;
        }
    };

    struct Script {
        std::vector<uint8_t> data;
        std::shared_ptr<std::string> filename;

        Script() {}
        Script(uint8_t *data, size_t size) : data(data, data + size) {}
        Script(std::initializer_list<uint8_t> init) : data(init) {}
    };

    struct Renderable {
        std::string name;

        Renderable() {}
        Renderable(std::string name) : name(name) {}
    };

    struct GlobalComponent {
        size_t globalCounter;
        std::shared_ptr<bool> test;

        GlobalComponent() : globalCounter(10) {}
        GlobalComponent(size_t initial_value) : globalCounter(initial_value) {}
    };
}; // namespace testing
