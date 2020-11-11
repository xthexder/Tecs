#pragma once

#include "test_ecs.hh"

#include <Tecs.hh>

namespace testing {
    void TestReadLock(Tecs::Lock<ECS, Tecs::Read<Transform>> lock);
    void TestWriteLock(Tecs::Lock<ECS, Tecs::Write<Transform>> lock);
    void TestAddRemoveLock(Tecs::Lock<ECS, Tecs::AddRemove> lock);
} // namespace testing
