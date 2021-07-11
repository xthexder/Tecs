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

template<>
struct Tecs::is_global_component<testing::GlobalComponent> : std::true_type {};
