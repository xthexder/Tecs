#pragma once

#include "test_ecs.hh"

#include <Tecs.hh>

namespace testing {
    template<typename... Tn, typename LockType>
    static inline void AssertHas(LockType &lock, Tecs::Entity &e) {
        if (Tecs::contains<Transform, Tn...>()) {
            Assert(lock.Has<Transform>(e), "Entity is missing a Transform component");
        } else {
            Assert(!lock.Has<Transform>(e), "Entity should not have a Transform component");
        }
        if (Tecs::contains<Renderable, Tn...>()) {
            Assert(lock.Has<Renderable>(e), "Entity is missing a Renderable component");
        } else {
            Assert(!lock.Has<Renderable>(e), "Entity should not have a Renderable component");
        }
        if (Tecs::contains<Script, Tn...>()) {
            Assert(lock.Has<Script>(e), "Entity is missing a Script component");
        } else {
            Assert(!lock.Has<Script>(e), "Entity should not have a Script component");
        }
        Assert(lock.Has<Tn...>(e), "Entity is missing components component");
    }

    void TestReadLock(Tecs::Lock<ECS, Tecs::Read<Transform>> lock);
    void TestWriteLock(Tecs::Lock<ECS, Tecs::Write<Transform>> lock);
    void TestAddRemoveLock(Tecs::Lock<ECS, Tecs::AddRemove> lock);
} // namespace testing
