#pragma once

#include "test_ecs.hh"
#include "transform_component.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace testing {
    struct Script {
        std::vector<uint32_t> data;
        std::shared_ptr<std::string> filename;

        Script() {}
        Script(uint32_t *data, size_t size) : data(data, data + size) {}
        Script(std::initializer_list<uint32_t> init) : data(init) {}
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

TECS_NAME_COMPONENT(testing::Script, "Script");
TECS_NAME_COMPONENT(testing::Renderable, "Renderable");
TECS_NAME_COMPONENT(testing::GlobalComponent, "GlobalComponent");
