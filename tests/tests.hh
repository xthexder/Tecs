#pragma once

#include "test_ecs.hh"

#include <Tecs.hh>

namespace testing {
    void TestReadLock(Tecs::ReadLock<ECS, Transform> lock);
    void TestWriteLock(Tecs::WriteLock<ECS, Transform> lock);
    void TestAddRemoveLock(Tecs::AddRemoveLock<ECS> lock);
} // namespace testing
