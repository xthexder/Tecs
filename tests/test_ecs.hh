#pragma once

#include "utils.hh"

#include <Tecs.hh>

namespace testing {
    struct Transform;
    struct Renderable;
    struct Script;
    struct GlobalComponent;

    using ECS = Tecs::ECS<Transform, Renderable, Script, GlobalComponent>;
}; // namespace testing

TECS_GLOBAL_COMPONENT(testing::GlobalComponent);
