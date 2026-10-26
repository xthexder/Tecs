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

TECS_NAME_COMPONENT(testing::Transform, "Transform");
TECS_NAME_COMPONENT(testing::Renderable, "Renderable");
TECS_NAME_COMPONENT(testing::Script, "Script");
TECS_NAME_COMPONENT(testing::GlobalComponent, "GlobalComponent");
