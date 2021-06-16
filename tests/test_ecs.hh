#pragma once

#include "utils.hh"

#include <Tecs.hh>

namespace testing {
    struct Transform;
    struct Renderable;
    struct Script;

    using ECS = Tecs::ECS<Transform, Renderable, Script>;
}; // namespace testing
