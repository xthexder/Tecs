#pragma once

#include "test_ecs.hh"

#include <Tecs.hh>

namespace testing {
    template<typename... Tn, typename LockType>
    static inline void AssertHas(LockType &lock, Tecs::Entity &e) {
        if (Tecs::contains<Transform, Tn...>()) {
            Assert(e.template Has<Transform>(lock), "Entity is missing a Transform component");
        } else {
            Assert(!e.template Has<Transform>(lock), "Entity should not have a Transform component");
        }
        if (Tecs::contains<Renderable, Tn...>()) {
            Assert(e.template Has<Renderable>(lock), "Entity is missing a Renderable component");
        } else {
            Assert(!e.template Has<Renderable>(lock), "Entity should not have a Renderable component");
        }
        if (Tecs::contains<Script, Tn...>()) {
            Assert(e.template Has<Script>(lock), "Entity is missing a Script component");
        } else {
            Assert(!e.template Has<Script>(lock), "Entity should not have a Script component");
        }
        if constexpr (sizeof...(Tn) > 0) {
            Assert(e.template Has<Tn...>(lock), "Entity is missing components");
        } else if (!e.Exists(lock)) {
            Assert(!e.Has<>(lock), "Invalid entity should not have components");
        }
    }

    void TestReadLock(Tecs::Lock<ECS, Tecs::Read<Transform>> lock);
    void TestWriteLock(Tecs::Lock<ECS, Tecs::Write<Transform>> lock);
    void TestAddRemoveLock(Tecs::Lock<ECS, Tecs::AddRemove> lock);
} // namespace testing
