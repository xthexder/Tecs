#include "tests.hh"

#include "test_components.hh"
#include "test_ecs.hh"
#include "utils.hh"

#include <Tecs.hh>
#include <chrono>
#include <cstring>
#include <future>

using namespace testing;

static ECS ecs;

#define ENTITY_COUNT 10000

int main(int argc, char **argv) {
    std::cout << "Running with " << ecs.GetComponentCount() << " component types" << std::endl;

    Tecs::Observer<ECS, Tecs::EntityAdded> entityAddedObserver;
    Tecs::Observer<ECS, Tecs::EntityRemoved> entityRemovedObserver;
    Tecs::Observer<ECS, Tecs::Added<Transform>> transformAddedObserver;
    Tecs::Observer<ECS, Tecs::Removed<Transform>> transformRemovedObserver;
    {
        Timer t("Test creating new observers");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        entityAddedObserver = writeLock.Watch<Tecs::EntityAdded>();
        entityRemovedObserver = writeLock.Watch<Tecs::EntityRemoved>();
        transformAddedObserver = writeLock.Watch<Tecs::Added<Transform>>();
        transformRemovedObserver = writeLock.Watch<Tecs::Removed<Transform>>();
    }
    {
        Timer t("Test adding each component type");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        for (size_t i = 0; i < ENTITY_COUNT; i++) {
            Tecs::Entity e = writeLock.NewEntity();
            Assert(e.id == i, "Expected Nth entity id to be " + std::to_string(i) + ", was " + std::to_string(e.id));
            Assert(!writeLock.Has<Transform, Renderable, Script>(e), "Entity must start with no components");

            // Test adding each component type
            Transform value(1.0, 0.0, 0.0, 1);
            writeLock.Set<Transform>(e, value);
            Assert(writeLock.Has<Transform>(e), "Entity should have a Transform component");
            Assert(!writeLock.Has<Renderable, Script>(e), "Entity has extra components");

            // Test making some changes to ensure values are copied
            value.pos[0] = 2.0;
            auto &transform = writeLock.Get<Transform>(e);
            transform.pos[0] = 0.0;

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
        Timer t("Test add remove entities in single transaction");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        for (size_t i = 0; i < 100; i++) {
            Tecs::Entity e = writeLock.NewEntity();
            Assert(e.id == ENTITY_COUNT + i,
                "Expected Nth entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " + std::to_string(e.id));
            Assert(!writeLock.Has<Transform, Renderable, Script>(e), "Entity must start with no components");

            writeLock.Set<Transform>(e, 1.0, 3.0, 3.0, 7);
            Assert(writeLock.Has<Transform>(e), "Entity should have a Transform component");
            Assert(!writeLock.Has<Renderable, Script>(e), "Entity has extra components");

            // Try removing an entity in the same transaction it was created in
            writeLock.Unset<Transform>(e);
            Assert(!writeLock.Has<Transform, Renderable, Script>(e), "Entity has extra components");
            e.Destroy(writeLock);
            Assert(!writeLock.Exists(e), "Entity still exists");
        }
    }
    {
        Timer t("Test add remove entities in two transactions");
        std::vector<Tecs::Entity> entityList;
        {
            auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
            for (size_t i = 0; i < 100; i++) {
                Tecs::Entity e = writeLock.NewEntity();
                Assert(e.id == ENTITY_COUNT + i,
                    "Expected Nth entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(e.id));
                Assert(!writeLock.Has<Transform, Renderable, Script>(e), "Entity must start with no components");

                entityList.emplace_back(e);

                writeLock.Set<Transform>(e, 1.0, 3.0, 3.0, 7);
                Assert(writeLock.Has<Transform>(e), "Entity should have a Transform component");
                Assert(!writeLock.Has<Renderable, Script>(e), "Entity has extra components");

                // Try setting the value twice in one transaction
                writeLock.Set<Transform>(e, 3.0, 1.0, 7.0, 3);
            }
        }
        {
            auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
            for (Tecs::Entity &e : entityList) {
                e.Destroy(writeLock);
            }
        }
    }
    {
        Timer t("Test reading observers");
        auto readLock = ecs.StartTransaction<>();
        {
            Tecs::EntityAdded entityAdded;
            for (size_t i = 0; i < ENTITY_COUNT; i++) {
                Assert(entityAddedObserver.Poll(readLock, entityAdded), "Expected another event #" + std::to_string(i));
                Assert(entityAdded.entity.id == i,
                    "Expected Entity id to be " + std::to_string(i) + ", was " + std::to_string(entityAdded.entity.id));
            }
            for (size_t i = 0; i < 100; i++) {
                Assert(entityAddedObserver.Poll(readLock, entityAdded),
                    "Expected another event #" + std::to_string(ENTITY_COUNT + i));
                Assert(entityAdded.entity.id == ENTITY_COUNT + i,
                    "Expected Entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(entityAdded.entity.id));
            }
            Assert(!entityAddedObserver.Poll(readLock, entityAdded), "Too many events triggered");
        }
        {
            Tecs::Added<Transform> transformAdded;
            for (size_t i = 0; i < ENTITY_COUNT; i++) {
                Assert(transformAddedObserver.Poll(readLock, transformAdded),
                    "Expected another event #" + std::to_string(i));
                Assert(transformAdded.entity.id == i,
                    "Expected Entity id to be " + std::to_string(i) + ", was " +
                        std::to_string(transformAdded.entity.id));
                Assert(transformAdded.component == Transform(0.0, 0.0, 0.0, 1),
                    "Expected component to be origin transform");
            }
            for (size_t i = 0; i < 100; i++) {
                Assert(transformAddedObserver.Poll(readLock, transformAdded),
                    "Expected another event #" + std::to_string(ENTITY_COUNT + i));
                Assert(transformAdded.entity.id == ENTITY_COUNT + i,
                    "Expected Entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(transformAdded.entity.id));
                Assert(transformAdded.component == Transform(3.0, 1.0, 7.0, 3),
                    "Expected component to be initial transform");
            }
            Assert(!transformAddedObserver.Poll(readLock, transformAdded), "Too many events triggered");
        }
        {
            Tecs::Removed<Transform> transformRemoved;
            for (size_t i = 0; i < 100; i++) {
                Assert(transformRemovedObserver.Poll(readLock, transformRemoved),
                    "Expected another event #" + std::to_string(i));
                Assert(transformRemoved.entity.id == ENTITY_COUNT + i,
                    "Expected Entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(transformRemoved.entity.id));
                Assert(transformRemoved.component == Transform(3.0, 1.0, 7.0, 3),
                    "Expected renderable name to be updated transform");
            }
            Assert(!transformRemovedObserver.Poll(readLock, transformRemoved), "Too many events triggered");
        }
        {
            Tecs::EntityRemoved entityRemoved;
            for (size_t i = 0; i < 100; i++) {
                Assert(entityRemovedObserver.Poll(readLock, entityRemoved),
                    "Expected another event #" + std::to_string(ENTITY_COUNT + i));
                Assert(entityRemoved.entity.id == ENTITY_COUNT + i,
                    "Expected Entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(entityRemoved.entity.id));
            }
            Assert(!entityRemovedObserver.Poll(readLock, entityRemoved), "Too many events triggered");
        }
    }
    {
        Timer t("Test read-modify-write values");
        // Read locks can be created after a write lock without deadlock, but not the other way around.
        auto writeLock = ecs.StartTransaction<Tecs::Write<Transform>>();
        size_t entityCount = 0;
        for (Tecs::Entity e : writeLock.EntitiesWith<Transform>()) {
            // Both current and previous values can be read at the same time.
            auto &currentTransform = writeLock.Get<Transform>(e);
            auto &previousTransform = writeLock.GetPrevious<Transform>(e);
            currentTransform.pos[0] = previousTransform.pos[0] + 1;
            currentTransform.pos[0] = previousTransform.pos[0] + 1;

            Assert(writeLock.GetPrevious<Transform>(e).pos[0] == 0, "Expected previous position.x to be 0");
            Assert(writeLock.Get<Transform>(e).pos[0] == 1, "Expected current position.x to be 1");
            entityCount++;
        }
        Assert(entityCount == ENTITY_COUNT, "Didn't see enough entities with Transform");
    }
    {
        Timer t("Test write was committed");
        auto readLock = ecs.StartTransaction<Tecs::Read<Transform>>();
        size_t entityCount = 0;
        for (Tecs::Entity e : readLock.EntitiesWith<Transform>()) {
            Assert(readLock.Get<Transform>(e).pos[0] == 1, "Expected previous position.x to be 1");
            entityCount++;
        }
        Assert(entityCount == ENTITY_COUNT, "Didn't see enough entities with Transform");
    }
    {
        Timer t("Test lock reference counting");
        std::unique_ptr<Tecs::Lock<ECS, Tecs::Write<Script>>> outerLock;
        Tecs::Entity writtenId;
        {
            auto transaction = ecs.StartTransaction<Tecs::Write<Script>>();
            writtenId = transaction.EntitiesWith<Script>()[0];
            writtenId.Get<Script>(transaction).data[3] = 99;

            outerLock.reset(new Tecs::Lock<ECS, Tecs::Write<Script>>(transaction));
        }
        // Transaction should not be commited yet.
        {
            // Try reading the written bit to make sure the write transaction is not yet commited.
            auto transaction = ecs.StartTransaction<Tecs::Read<Script>>();
            Assert(writtenId.Get<Script>(transaction).data[3] != 99, "Script data should not be set to 99");
        }
        outerLock.reset(nullptr);
        // Transaction should now be commited.
        {
            // Try reading the written bit to make sure the write transaction is commited.
            auto transaction = ecs.StartTransaction<Tecs::Read<Script>>();
            Assert(writtenId.Get<Script>(transaction).data[3] == 99, "Script data should be set to 99");
        }
    }
    {
        Timer t("Test read lock reference write transaction can see changes");
        auto transaction = ecs.StartTransaction<Tecs::Write<Script>>();
        Tecs::Entity e = transaction.EntitiesWith<Script>()[0];
        e.Get<Script>(transaction).data[3] = 88;
        Tecs::Lock<ECS, Tecs::Read<Script>> lock = transaction;
        Assert(e.Get<Script>(lock).data[3] == 88, "Script data should be set to 88");
    }
    {
        Timer t("Test reading observers again");
        auto readLock = ecs.StartTransaction<>();
        Tecs::EntityAdded entityAdded;
        Tecs::EntityRemoved entityRemoved;
        Tecs::Added<Transform> transformAdded;
        Tecs::Removed<Transform> transformRemoved;
        Assert(!entityAddedObserver.Poll(readLock, entityAdded), "No events should have occured");
        Assert(!entityRemovedObserver.Poll(readLock, entityRemoved), "No events should have occured");
        Assert(!transformAddedObserver.Poll(readLock, transformAdded), "No events should have occured");
        Assert(!transformRemovedObserver.Poll(readLock, transformRemoved), "No events should have occured");
    }
    {
        Timer t("Test stopping observers");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        entityAddedObserver.Stop(writeLock);
        entityRemovedObserver.Stop(writeLock);
        transformAddedObserver.Stop(writeLock);
        transformRemovedObserver.Stop(writeLock);
    }
    {
        Timer t("Test reading observers again");
        auto readLock = ecs.StartTransaction<>();
        Tecs::EntityAdded entityAdded;
        Tecs::EntityRemoved entityRemoved;
        Tecs::Added<Transform> transformAdded;
        Tecs::Removed<Transform> transformRemoved;
        Assert(!entityAddedObserver.Poll(readLock, entityAdded), "No events should have occured");
        Assert(!entityRemovedObserver.Poll(readLock, entityRemoved), "No events should have occured");
        Assert(!transformAddedObserver.Poll(readLock, transformAdded), "No events should have occured");
        Assert(!transformRemovedObserver.Poll(readLock, transformRemoved), "No events should have occured");
    }
    {
        Timer t("Test remove while iterating");
        // Read locks can be created after a write lock without deadlock, but not the other way around.
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        auto &entities = writeLock.EntitiesWith<Transform>();
        size_t prevSize = entities.size();
        for (size_t i = 0; i < entities.size() && i < 100; i++) {
            entities[i].Destroy(writeLock);

            Assert(!entities[i], "Entity in list should not be valid after removal.");
            Assert(entities.size() == prevSize, "Entity list should not change size during iteration.");
        }
    }
    {
        Timer t("Test add while iterating");
        // Read locks can be created after a write lock without deadlock, but not the other way around.
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        auto &entities = writeLock.EntitiesWith<Transform>();
        size_t prevSize = entities.size();
        for (size_t i = 0; i < 100; i++) {
            Tecs::Entity e = writeLock.NewEntity();
            Assert(entities.size() == prevSize, "Entity list should not change size during iteration.");

            e.Set<Transform>(writeLock, 1.0, 0.0, 0.0, 1);
            Assert(entities.size() == prevSize, "Entity list should not change size during iteration.");
        }
        Assert(writeLock.EntitiesWith<Transform>().size() == prevSize + 100,
            "Entity list should be updated for later calls.");
    }
    {
        Timer t("Test write priority");
        std::vector<std::thread> readThreads;
        std::atomic_int counter(0);
        for (int i = 0; i < 100; i++) {
            readThreads.emplace_back([&counter, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(i));
                auto readLock = ecs.StartTransaction<Tecs::Read<Transform>>();
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            auto writeLock = ecs.StartTransaction<Tecs::Write<Transform>>();
            Assert(counter < 100, "Writer lock did not take priority over readers");
        }
        for (auto &thread : readThreads) {
            thread.join();
        }
    }
    {
        Timer t("Test read lock typecasting");
        auto readLockAll = ecs.StartTransaction<Tecs::Read<Transform, Renderable, Script>>();
        { // Test Subset() method
            auto readLockTransform = readLockAll.Subset<Tecs::Read<Transform>>();
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = readLockAll.Subset<Tecs::Read<Script, Renderable>>();
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::Lock<ECS, Tecs::Read<Transform>> readLockTransform = readLockAll;
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Read<Script, Renderable>> readLockScript = readLockAll;
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        TestReadLock(readLockAll);
    }
    {
        Timer t("Test component write lock typecasting");
        auto writeLockAll = ecs.StartTransaction<Tecs::Write<Transform, Renderable, Script>>();
        { // Test Subset() method
            auto readLockTransform = writeLockAll.Subset<Tecs::Read<Transform>>();
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = writeLockAll.Subset<Tecs::Read<Script, Renderable>>();
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }

            auto writeLockTransform = writeLockAll.Subset<Tecs::Write<Transform>>();
            for (Tecs::Entity e : writeLockTransform.EntitiesWith<Transform>()) {
                Assert(writeLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto writeLockScript = writeLockAll.Subset<Tecs::Write<Script, Renderable>>();
            for (Tecs::Entity e : writeLockScript.EntitiesWith<Script>()) {
                Assert(writeLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::Lock<ECS, Tecs::Read<Transform>> readLockTransform = writeLockAll;
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Read<Script, Renderable>> readLockScript = writeLockAll;
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }

            Tecs::Lock<ECS, Tecs::Write<Transform>> writeLockTransform = writeLockAll;
            for (Tecs::Entity e : writeLockTransform.EntitiesWith<Transform>()) {
                Assert(writeLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Write<Script, Renderable>> writeLockScript = writeLockAll;
            for (Tecs::Entity e : writeLockScript.EntitiesWith<Script>()) {
                Assert(writeLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        TestReadLock(writeLockAll);
        TestWriteLock(writeLockAll);
    }
    {
        Timer t("Test entity write lock typecasting");
        auto writeLockAll = ecs.StartTransaction<Tecs::AddRemove>();
        { // Test Subset() method
            auto readLockTransform = writeLockAll.Subset<Tecs::Read<Transform>>();
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = writeLockAll.Subset<Tecs::Read<Script, Renderable>>();
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }

            auto writeLockTransform = writeLockAll.Subset<Tecs::Write<Transform>>();
            for (Tecs::Entity e : writeLockTransform.EntitiesWith<Transform>()) {
                Assert(writeLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            auto writeLockScript = writeLockAll.Subset<Tecs::Write<Script, Renderable>>();
            for (Tecs::Entity e : writeLockScript.EntitiesWith<Script>()) {
                Assert(writeLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::Lock<ECS, Tecs::Read<Transform>> readLockTransform = writeLockAll;
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(readLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Read<Script, Renderable>> readLockScript = writeLockAll;
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(readLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }

            Tecs::Lock<ECS, Tecs::Write<Transform>> writeLockTransform = writeLockAll;
            for (Tecs::Entity e : writeLockTransform.EntitiesWith<Transform>()) {
                Assert(writeLockTransform.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Write<Script, Renderable>> writeLockScript = writeLockAll;
            for (Tecs::Entity e : writeLockScript.EntitiesWith<Script>()) {
                Assert(writeLockScript.Get<Script>(e).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        TestReadLock(writeLockAll);
        TestWriteLock(writeLockAll);
        TestAddRemoveLock(writeLockAll);
    }
    {
        Timer t("Test reading observers again");
        auto readLock = ecs.StartTransaction<>();
        Tecs::EntityAdded entityAdded;
        Tecs::EntityRemoved entityRemoved;
        Tecs::Added<Transform> transformAdded;
        Tecs::Removed<Transform> transformRemoved;
        Assert(!entityAddedObserver.Poll(readLock, entityAdded), "No events should have occured");
        Assert(!entityRemovedObserver.Poll(readLock, entityRemoved), "No events should have occured");
        Assert(!transformAddedObserver.Poll(readLock, transformAdded), "No events should have occured");
        Assert(!transformRemovedObserver.Poll(readLock, transformRemoved), "No events should have occured");
    }

    return 0;
}

namespace testing {
    void TestReadLock(Tecs::Lock<ECS, Tecs::Read<Transform>> lock) {
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            Assert(lock.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
        }
    }

    void TestWriteLock(Tecs::Lock<ECS, Tecs::Write<Transform>> lock) {
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            Assert(lock.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
        }
    }

    void TestAddRemoveLock(Tecs::Lock<ECS, Tecs::AddRemove> lock) {
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            Assert(lock.Get<Transform>(e).pos[0] == 1, "Expected position.x to be 1");
            Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
        }
    }
} // namespace testing
