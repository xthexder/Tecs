#include "test_components.hh"
#include "utils.hh"

#include <Tecs.hh>
#include <chrono>
#include <cstring>
#include <future>

using namespace testing;

using ECSType = Tecs::ECS<Transform, Renderable, Script>;
static ECSType ecs;

#define ENTITY_COUNT 10000

static void TestReadLock(Tecs::ReadLockRef<ECSType, Transform> lock) {
    for (Tecs::Entity e : lock.ValidEntities<Transform>()) {
        Assert(lock.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
        Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
    }
}

static void TestWriteLock(Tecs::WriteLockRef<ECSType, Transform> lock) {
    for (Tecs::Entity e : lock.ValidEntities<Transform>()) {
        Assert(lock.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
        Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
    }
}

static void TestAddRemoveLock(Tecs::AddRemoveLockRef<ECSType> lock) {
    for (Tecs::Entity e : lock.ValidEntities<Transform>()) {
        Assert(lock.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
        Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
    }
}

int main(int argc, char **argv) {
    std::cout << "Running with " << ecs.GetComponentCount() << " component types" << std::endl;

    {
        Timer t("Test adding each component type");
        auto writeLock = ecs.AddRemoveEntities();
        for (size_t i = 0; i < ENTITY_COUNT; i++) {
            Tecs::Entity e = writeLock.AddEntity();
            Assert(!writeLock.Has<Transform, Renderable, Script>(e), "Entity must start with no components");

            // Test adding each component type
            Transform value(0.0, 0.0, 0.0, 1);
            writeLock.Set<Transform>(e, value);
            Assert(writeLock.Has<Transform>(e), "Entity should have a Transform component");
            Assert(!writeLock.Has<Renderable, Script>(e), "Entity has extra components");

            writeLock.Set<Renderable>(e, "entity" + std::to_string(i));
            Assert(writeLock.Has<Transform, Renderable>(e), "Entity should have a Transform and Renderable component");
            Assert(!writeLock.Has<Script>(e), "Entity has extra components");

            writeLock.Set<Script>(e, std::initializer_list<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0}));
            Assert(writeLock.Has<Transform, Renderable, Script>(e), "Entity should have all components");

            // Test removing a component
            writeLock.Unset<Renderable>(e);
            Assert(writeLock.Has<Transform, Script>(e), "Entity should have a Transform and Script component");
            Assert(!writeLock.Has<Renderable>(e), "Entity should not have a Renderable component");

            // Test references work after Set()
            auto &script = writeLock.Get<Script>(e);
            Assert(script.data.size() == 8, "Script component should have size 8");
            Assert(script.data[0] == 0, "Script component should have value [(0), 0, 0, 0, 0, 0, 0, 0]");
            Assert(script.data[1] == 0, "Script component should have value [0, (0), 0, 0, 0, 0, 0, 0]");
            Assert(script.data[2] == 0, "Script component should have value [0, 0, (0), 0, 0, 0, 0, 0]");
            Assert(script.data[3] == 0, "Script component should have value [0, 0, 0, (0), 0, 0, 0, 0]");
            Assert(script.data[4] == 0, "Script component should have value [0, 0, 0, 0, (0), 0, 0, 0]");
            Assert(script.data[5] == 0, "Script component should have value [0, 0, 0, 0, 0, (0), 0, 0]");
            Assert(script.data[6] == 0, "Script component should have value [0, 0, 0, 0, 0, 0, (0), 0]");
            Assert(script.data[7] == 0, "Script component should have value [0, 0, 0, 0, 0, 0, 0, (0)]");

            writeLock.Set<Script>(e, std::initializer_list<uint8_t>({1, 2, 3, 4}));
            Assert(writeLock.Has<Transform, Script>(e), "Entity should have a Transform and Script component");
            Assert(!writeLock.Has<Renderable>(e), "Entity should not have a Renderable component");

            Assert(script.data.size() == 4, "Script component should have size 4");
            Assert(script.data[0] == 1, "Script component should have value [(1), 2, 3, 4]");
            Assert(script.data[1] == 2, "Script component should have value [1, (2), 3, 4]");
            Assert(script.data[2] == 3, "Script component should have value [1, 2, (3), 4]");
            Assert(script.data[3] == 4, "Script component should have value [1, 2, 3, (4)]");
        }
    }
    {
        Timer t("Test reading previous values");
        // Read locks can be created after a write lock without deadlock, but not the other way around.
        auto writeLock = ecs.WriteEntitiesWith<Transform>();
        for (Tecs::Entity e : writeLock.ValidEntities<Transform>()) {
            // Both current and previous values can be read at the same time.
            auto &currentTransform = writeLock.Get<Transform>(e);
            auto &previousTransform = writeLock.GetPrevious<Transform>(e);
            currentTransform.pos[0] = previousTransform.pos[0] + 1;
            currentTransform.pos[0] = previousTransform.pos[0] + 1;

            Assert(writeLock.GetPrevious<Transform>(e).pos[0] == 0, "Expected previous position.x to be 0");
            Assert(writeLock.Get<Transform>(e).pos[0] == 1, "Expected current position.x to be 1");
        }
    }
    {
        Timer t("Test write was committed");
        auto readLock = ecs.ReadEntitiesWith<Transform>();
        for (Tecs::Entity e : readLock.ValidEntities<Transform>()) {
            Assert(readLock.Get<Transform>(e).pos[0] == 1, "Expected previous position.x to be 1");
        }
    }
    {
        Timer t("Test write priority");
        std::vector<std::thread> readThreads;
        std::atomic_int counter(0);
        for (int i = 0; i < 100; i++) {
            readThreads.emplace_back([&counter, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(i));
                auto readLock = ecs.ReadEntitiesWith<Transform>();
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            auto writeLock = ecs.WriteEntitiesWith<Transform>();
            Assert(counter < 100, "Writer lock did not take priority over readers");
        }
        for (auto &thread : readThreads) {
            thread.join();
        }
    }
    {
        Timer t("Test read lock typecasting");
        auto readLockAll = ecs.ReadEntitiesWith<Transform, Renderable, Script>();
        { // Test Subset() method
            auto readLockTransform = readLockAll.Subset<Transform>();
            for (Tecs::Entity e : readLockTransform.ValidEntities<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = readLockAll.Subset<Script, Renderable>();
            for (Tecs::Entity e : readLockScript.ValidEntities<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::ReadLockRef<ECSType, Transform> readLockTransform = readLockAll;
            for (Tecs::Entity e : readLockTransform.ValidEntities<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::ReadLockRef<ECSType, Script, Renderable> readLockScript = readLockAll;
            for (Tecs::Entity e : readLockScript.ValidEntities<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        TestReadLock(readLockAll);
    }
    {
        Timer t("Test component write lock typecasting");
        auto writeLockAll = ecs.WriteEntitiesWith<Transform, Renderable, Script>();
        { // Test Subset() method
            auto readLockTransform = writeLockAll.ReadLock<Transform>();
            for (Tecs::Entity e : readLockTransform.ValidEntities<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = writeLockAll.ReadLock<Script, Renderable>();
            for (Tecs::Entity e : readLockScript.ValidEntities<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }

            auto writeLockTransform = writeLockAll.Subset<Transform>();
            for (Tecs::Entity e : writeLockTransform.ValidEntities<Transform>()) {
                Assert(writeLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto writeLockScript = writeLockAll.Subset<Script, Renderable>();
            for (Tecs::Entity e : writeLockScript.ValidEntities<Script>()) {
                Assert(writeLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::ReadLockRef<ECSType, Transform> readLockTransform = writeLockAll;
            for (Tecs::Entity e : readLockTransform.ValidEntities<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::ReadLockRef<ECSType, Script, Renderable> readLockScript = writeLockAll;
            for (Tecs::Entity e : readLockScript.ValidEntities<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }

            Tecs::WriteLockRef<ECSType, Transform> writeLockTransform = writeLockAll;
            for (Tecs::Entity e : writeLockTransform.ValidEntities<Transform>()) {
                Assert(writeLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::WriteLockRef<ECSType, Script, Renderable> writeLockScript = writeLockAll;
            for (Tecs::Entity e : writeLockScript.ValidEntities<Script>()) {
                Assert(writeLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        TestReadLock(writeLockAll);
        TestWriteLock(writeLockAll);
    }
    {
        Timer t("Test entity write lock typecasting");
        auto writeLockAll = ecs.AddRemoveEntities();
        { // Test Subset() method
            auto writeLockTransform = writeLockAll.WriteLock<Transform>();
            for (Tecs::Entity e : writeLockTransform.ValidEntities<Transform>()) {
                Assert(writeLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto writeLockScript = writeLockAll.WriteLock<Script, Renderable>();
            for (Tecs::Entity e : writeLockScript.ValidEntities<Script>()) {
                Assert(writeLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }

            auto readLockTransform = writeLockAll.ReadLock<Transform>();
            for (Tecs::Entity e : readLockTransform.ValidEntities<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = writeLockAll.ReadLock<Script, Renderable>();
            for (Tecs::Entity e : readLockScript.ValidEntities<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::WriteLockRef<ECSType, Transform> writeLockTransform = writeLockAll;
            for (Tecs::Entity e : writeLockTransform.ValidEntities<Transform>()) {
                Assert(writeLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::WriteLockRef<ECSType, Script, Renderable> writeLockScript = writeLockAll;
            for (Tecs::Entity e : writeLockScript.ValidEntities<Script>()) {
                Assert(writeLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }

            Tecs::ReadLockRef<ECSType, Transform> readLockTransform = writeLockAll;
            for (Tecs::Entity e : readLockTransform.ValidEntities<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::ReadLockRef<ECSType, Script, Renderable> readLockScript = writeLockAll;
            for (Tecs::Entity e : readLockScript.ValidEntities<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        TestReadLock(writeLockAll);
        TestWriteLock(writeLockAll);
        TestAddRemoveLock(writeLockAll);
    }

    return 0;
}
