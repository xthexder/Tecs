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

int main(int /* argc */, char ** /* argv */) {
    std::cout << "Running with " << ENTITY_COUNT << " entities and " << ecs.GetComponentCount() << " component types"
              << std::endl;

    Tecs::Observer<ECS, Tecs::EntityEvent> entityObserver;
    Tecs::Observer<ECS, Tecs::ComponentEvent<Transform>> transformObserver;
    Tecs::Observer<ECS, Tecs::ComponentEvent<GlobalComponent>> globalCompObserver;
    {
        Timer t("Test creating new observers");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        entityObserver = writeLock.Watch<Tecs::EntityEvent>();
        transformObserver = writeLock.Watch<Tecs::ComponentEvent<Transform>>();
        globalCompObserver = writeLock.Watch<Tecs::ComponentEvent<GlobalComponent>>();
    }
    bool globalComponentInitialized = false;
    {
        Timer t("Test initializing global components");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        Assert(!writeLock.Has<GlobalComponent>(), "ECS must start with no global component");
        auto &gc = writeLock.Set<GlobalComponent>(0);
        Assert(writeLock.Has<GlobalComponent>(), "ECS should have a global component");
        Assert(gc.globalCounter == 0, "Global counter should be initialized to zero");
        gc.globalCounter++;

        globalComponentInitialized = true;
        gc.test = std::shared_ptr<bool>(&globalComponentInitialized, [](bool *b) {
            *b = false;
        });

        auto &gc2 = writeLock.Get<GlobalComponent>();
        Assert(gc2.globalCounter == 1, "Global counter should be read back as 1");
        Assert(globalComponentInitialized, "Global component should be initialized");
    }
    {
        Timer t("Test update global counter");
        auto writeLock = ecs.StartTransaction<Tecs::Write<GlobalComponent>>();
        Assert(writeLock.Has<GlobalComponent>(), "ECS should have a global component");

        auto &gc = writeLock.Get<GlobalComponent>();
        Assert(gc.globalCounter == 1, "Global counter should be read back as 1");
        gc.globalCounter++;

        Assert(globalComponentInitialized, "Global component should be initialized");
    }
    {
        Timer t("Test read global counter");
        auto readLock = ecs.StartTransaction<Tecs::Read<GlobalComponent>>();
        Assert(readLock.Has<GlobalComponent>(), "ECS should have a global component");

        auto &gc = readLock.Get<GlobalComponent>();
        Assert(gc.globalCounter == 2, "Global counter should be read back as 2");
    }
    {
        Timer t("Test remove global component");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        Assert(writeLock.Has<GlobalComponent>(), "ECS should have a global component");

        auto &gc = writeLock.Get<GlobalComponent>();
        Assert(gc.globalCounter == 2, "Global counter should be read back as 2");

        writeLock.Unset<GlobalComponent>();
        Assert(!writeLock.Has<GlobalComponent>(), "Global component should be removed");
        Assert(writeLock.Had<GlobalComponent>(), "ECS should still know previous state");
        Assert(globalComponentInitialized, "Global component should still be initialized (kept by read pointer)");
    }
    Assert(globalComponentInitialized, "Global component should still be initialized (kept by observer)");
    {
        Timer t("Test add remove global component in single transaction");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        Assert(!writeLock.Has<GlobalComponent>(), "Global component should be removed");

        auto &gc = writeLock.Get<GlobalComponent>();
        Assert(writeLock.Has<GlobalComponent>(), "Get call should have initialized global component");
        Assert(gc.globalCounter == 10, "Global counter should be default initialized to 10");

        bool compInitialized = true;
        gc.test = std::shared_ptr<bool>(&compInitialized, [](bool *b) {
            *b = false;
        });

        // Try removing the component in the same transaction it was created
        writeLock.Unset<GlobalComponent>();
        Assert(!writeLock.Has<GlobalComponent>(), "Global component should be removed");

        Assert(!compInitialized, "Global component should be deconstructed immediately");
    }
    {
        Timer t("Test adding each component type");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        for (size_t i = 0; i < ENTITY_COUNT; i++) {
            Tecs::Entity e = writeLock.NewEntity();
            Assert(e.id == i, "Expected Nth entity id to be " + std::to_string(i) + ", was " + std::to_string(e.id));
            AssertHas<>(writeLock, e);

            // Test adding each component type
            Transform value(1.0, 0.0, 0.0, 1);
            e.Set<Transform>(writeLock, value);
            AssertHas<Transform>(writeLock, e);

            // Test making some changes to ensure values are copied
            value.pos[0] = 2.0;
            auto &transform = e.Get<Transform>(writeLock);
            transform.pos[0] = 0.0;

            e.Set<Renderable>(writeLock, "entity" + std::to_string(i));
            AssertHas<Transform, Renderable>(writeLock, e);

            e.Set<Script>(writeLock, std::initializer_list<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0}));
            AssertHas<Transform, Renderable, Script>(writeLock, e);

            // Test removing a component
            e.Unset<Renderable>(writeLock);
            AssertHas<Transform, Script>(writeLock, e);

            // Test references work after Set()
            auto &script = e.Get<Script>(writeLock);
            Assert(script.data.size() == 8, "Script component should have size 8");
            Assert(script.data[0] == 0, "Script component should have value [(0), 0, 0, 0, 0, 0, 0, 0]");
            Assert(script.data[1] == 0, "Script component should have value [0, (0), 0, 0, 0, 0, 0, 0]");
            Assert(script.data[2] == 0, "Script component should have value [0, 0, (0), 0, 0, 0, 0, 0]");
            Assert(script.data[3] == 0, "Script component should have value [0, 0, 0, (0), 0, 0, 0, 0]");
            Assert(script.data[4] == 0, "Script component should have value [0, 0, 0, 0, (0), 0, 0, 0]");
            Assert(script.data[5] == 0, "Script component should have value [0, 0, 0, 0, 0, (0), 0, 0]");
            Assert(script.data[6] == 0, "Script component should have value [0, 0, 0, 0, 0, 0, (0), 0]");
            Assert(script.data[7] == 0, "Script component should have value [0, 0, 0, 0, 0, 0, 0, (0)]");

            e.Set<Script>(writeLock, std::initializer_list<uint8_t>({1, 2, 3, 4}));
            AssertHas<Transform, Script>(writeLock, e);

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
            AssertHas<>(writeLock, e);

            e.Set<Transform>(writeLock, 1.0, 3.0, 3.0, 7);
            AssertHas<Transform>(writeLock, e);

            e.Set<Renderable>(writeLock, "foo");
            AssertHas<Transform, Renderable>(writeLock, e);

            // Try removing an entity in the same transaction it was created in
            e.Unset<Transform>(writeLock);
            AssertHas<Renderable>(writeLock, e);

            e.Destroy(writeLock);
            Assert(!e.Exists(writeLock), "Entity still exists");
            AssertHas<>(writeLock, e);
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
                AssertHas<>(writeLock, e);

                entityList.emplace_back(e);

                e.Set<Transform>(writeLock, 1.0, 3.0, 3.0, 7);
                AssertHas<Transform>(writeLock, e);

                // Try setting the value twice in one transaction
                e.Set<Transform>(writeLock, 3.0, 1.0, 7.0, 3);
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
            Tecs::EntityEvent event;
            for (size_t i = 0; i < ENTITY_COUNT; i++) {
                Assert(entityObserver.Poll(readLock, event), "Expected another event #" + std::to_string(i));
                Assert(event.type == Tecs::EventType::ADDED, "Expected entity event type to be ADDED");
                Assert(event.entity.id == i,
                    "Expected Entity id to be " + std::to_string(i) + ", was " + std::to_string(event.entity.id));
            }
            for (size_t i = 0; i < 100; i++) {
                Assert(entityObserver.Poll(readLock, event),
                    "Expected another event #" + std::to_string(ENTITY_COUNT + i));
                Assert(event.type == Tecs::EventType::ADDED, "Expected entity event type to be ADDED");
                Assert(event.entity.id == ENTITY_COUNT + i,
                    "Expected Entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(event.entity.id));
            }
            for (size_t i = 0; i < 100; i++) {
                Assert(entityObserver.Poll(readLock, event),
                    "Expected another event #" + std::to_string(ENTITY_COUNT + i));
                Assert(event.type == Tecs::EventType::REMOVED, "Expected component event type to be REMOVED");
                Assert(event.entity.id == ENTITY_COUNT + i,
                    "Expected Entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(event.entity.id));
            }
            Assert(!entityObserver.Poll(readLock, event), "Too many events triggered");
        }
        {
            Tecs::ComponentEvent<Transform> event;
            for (size_t i = 0; i < ENTITY_COUNT; i++) {
                Assert(transformObserver.Poll(readLock, event), "Expected another event #" + std::to_string(i));
                Assert(event.type == Tecs::EventType::ADDED, "Expected component event type to be ADDED");
                Assert(event.entity.id == i,
                    "Expected Entity id to be " + std::to_string(i) + ", was " + std::to_string(event.entity.id));
                Assert(event.component == Transform(0.0, 0.0, 0.0, 1), "Expected component to be origin transform");
            }
            for (size_t i = 0; i < 100; i++) {
                Assert(transformObserver.Poll(readLock, event),
                    "Expected another event #" + std::to_string(ENTITY_COUNT + i));
                Assert(event.type == Tecs::EventType::ADDED, "Expected component event type to be ADDED");
                Assert(event.entity.id == ENTITY_COUNT + i,
                    "Expected Entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(event.entity.id));
                Assert(event.component == Transform(3.0, 1.0, 7.0, 3), "Expected component to be initial transform");
            }
            for (size_t i = 0; i < 100; i++) {
                Assert(transformObserver.Poll(readLock, event), "Expected another event #" + std::to_string(i));
                Assert(event.type == Tecs::EventType::REMOVED, "Expected component event type to be REMOVED");
                Assert(event.entity.id == ENTITY_COUNT + i,
                    "Expected Entity id to be " + std::to_string(ENTITY_COUNT + i) + ", was " +
                        std::to_string(event.entity.id));
                Assert(event.component == Transform(3.0, 1.0, 7.0, 3),
                    "Expected renderable name to be updated transform");
            }
            Assert(!transformObserver.Poll(readLock, event), "Too many events triggered");
        }
        {
            Tecs::ComponentEvent<GlobalComponent> event;
            Assert(globalCompObserver.Poll(readLock, event), "Expected a GlobalComponent created event");
            Assert(event.type == Tecs::EventType::ADDED, "Expected component event type to be ADDED");
            Assert(!event.entity, "Global component events should not have a valid entity");
            Assert(event.component.globalCounter == 1,
                "Global component should have been created with globalCounter = 1");

            Assert(globalCompObserver.Poll(readLock, event), "Expected a GlobalComponent removed event");
            Assert(event.type == Tecs::EventType::REMOVED, "Expected component event type to be REMOVED");
            Assert(!event.entity, "Global component events should not have a valid entity");
            Assert(event.component.globalCounter == 2,
                "Global component should have been removed with globalCounter = 2");

            Assert(!globalCompObserver.Poll(readLock, event), "Too many events triggered");
        }
        Assert(!globalComponentInitialized, "Global component should be deconstructed");
    }
    {
        Timer t("Test read-modify-write values");
        // Read locks can be created after a write lock without deadlock, but not the other way around.
        auto writeLock = ecs.StartTransaction<Tecs::Write<Transform>>();
        size_t entityCount = 0;
        for (Tecs::Entity e : writeLock.EntitiesWith<Transform>()) {
            // Both current and previous values can be read at the same time.
            auto &currentTransform = e.Get<Transform>(writeLock);
            auto &previousTransform = e.GetPrevious<Transform>(writeLock);
            currentTransform.pos[0] = previousTransform.pos[0] + 1;
            currentTransform.pos[0] = previousTransform.pos[0] + 1;

            Assert(e.GetPrevious<Transform>(writeLock).pos[0] == 0, "Expected previous position.x to be 0");
            Assert(e.Get<Transform>(writeLock).pos[0] == 1, "Expected current position.x to be 1");
            entityCount++;
        }
        Assert(entityCount == ENTITY_COUNT, "Didn't see enough entities with Transform");
    }
    {
        Timer t("Test write was committed");
        auto readLock = ecs.StartTransaction<Tecs::Read<Transform>>();
        size_t entityCount = 0;
        for (Tecs::Entity e : readLock.EntitiesWith<Transform>()) {
            Assert(e.Get<Transform>(readLock).pos[0] == 1, "Expected previous position.x to be 1");
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
        std::thread readThread([writtenId] {
            // Try reading the written bit to make sure the write transaction is not yet commited.
            auto transaction = ecs.StartTransaction<Tecs::Read<Script>>();
            Assert(writtenId.Get<Script>(transaction).data[3] != 99, "Script data should not be set to 99");
        });
        readThread.join();
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
        Tecs::EntityEvent entityEvent;
        Tecs::ComponentEvent<Transform> transformEvent;
        Tecs::ComponentEvent<GlobalComponent> globalCompEvent;
        Assert(!entityObserver.Poll(readLock, entityEvent), "No events should have occured");
        Assert(!transformObserver.Poll(readLock, transformEvent), "No events should have occured");
        Assert(!globalCompObserver.Poll(readLock, globalCompEvent), "No events should have occured");
    }
    {
        Timer t("Test stopping observers");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        entityObserver.Stop(writeLock);
        transformObserver.Stop(writeLock);
        globalCompObserver.Stop(writeLock);
    }
    {
        Timer t("Test reading observers again");
        auto readLock = ecs.StartTransaction<>();
        Tecs::EntityEvent entityEvent;
        Tecs::ComponentEvent<Transform> transformEvent;
        Tecs::ComponentEvent<GlobalComponent> globalCompEvent;
        Assert(!entityObserver.Poll(readLock, entityEvent), "No events should have occured");
        Assert(!transformObserver.Poll(readLock, transformEvent), "No events should have occured");
        Assert(!globalCompObserver.Poll(readLock, globalCompEvent), "No events should have occured");
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
        Timer t("Test add/remove entity priority");
        Tecs::Entity e;
        {
            auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
            e = writeLock.NewEntity();
            e.Set<Transform>(writeLock, 42.0, 1.0, 64.0, 99);
        }
        std::atomic_bool commitStart = false;
        std::atomic_bool commited = false;
        std::vector<std::thread> readThreads;
        readThreads.emplace_back([&e]() {
            auto readLock = ecs.StartTransaction<>();
            Assert(e.Exists(readLock), "The entity should exist for all transactions started before AddRemove.");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            Assert(e.Exists(readLock), "The entity should still exist for all transactions started before AddRemove.");
        });
        for (int i = 0; i < 100; i++) {
            readThreads.emplace_back([&commitStart, &commited, &e, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(i));
                auto readLock = ecs.StartTransaction<>();
                if (commited) {
                    Assert(!e.Exists(readLock), "The entity should already be removed at this point.");
                } else if (!commitStart) {
                    Assert(e.Exists(readLock), "The entity shouldn't be removed yet.");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    Assert(e.Exists(readLock), "The entity shouldn't be removed until after existing reads complete.");
                }
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
            e.Destroy(writeLock);
            Assert(!e.Exists(writeLock), "Entity should not exist after it is destroyed.");
            commitStart = true;
        }
        commited = true;
        {
            auto lock = ecs.StartTransaction<>();
            Assert(!e.Exists(lock), "Entity should not exist after test.");
        }
        for (auto &thread : readThreads) {
            thread.join();
        }
        {
            auto lock = ecs.StartTransaction<>();
            Assert(!e.Exists(lock), "Entity should not exist after test.");
        }
    }
    {
        Timer t("Test noop write transaction does not commit lock");
        {
            auto readLock = ecs.StartTransaction<Tecs::Read<Script>>();
            Tecs::Entity readId = readLock.EntitiesWith<Script>()[0];
            auto previousValue = readId.Get<Script>(readLock).data[3];

            std::thread readThread([readId, previousValue] {
                // Try starting a write transaction while another read transaction is active.
                auto writeLock = ecs.StartTransaction<Tecs::Write<Script>>();
                Assert(readId.GetPrevious<Script>(writeLock).data[3] == previousValue,
                    "Script data should match out read transaction");

                // Commit should not occur since no write operations were done.
                // Test would otherwise block on commit due to active read transaction.
            });
            readThread.join();
        }
    }
    {
        Timer t("Test write transaction does not commit untouched components");
        {
            auto readLock = ecs.StartTransaction<Tecs::Read<Script>>();
            Tecs::Entity readId = readLock.EntitiesWith<Script>()[0];
            auto previousValue = readId.Get<Script>(readLock).data[3];

            std::thread readThread([readId, previousValue] {
                // Try starting a write transaction while another read transaction is active.
                auto writeLock = ecs.StartTransaction<Tecs::Write<Transform, Script>>();
                Tecs::Entity writeId = writeLock.EntitiesWith<Transform>()[0];
                writeId.Get<Transform>(writeLock).pos[2]++;

                Assert(readId.GetPrevious<Script>(writeLock).data[3] == previousValue,
                    "Script data should match out read transaction");

                // Commit should only occur on Transform component
                // Test would otherwise block on Transform commit due to active read transaction.
            });
            readThread.join();
        }
    }
    {
        Timer t("Test noop add/remove transaction does not commit");
        {
            auto readLock = ecs.StartTransaction<Tecs::ReadAll>();
            Tecs::Entity readId = readLock.EntitiesWith<Script>()[0];
            auto previousValue = readId.Get<Script>(readLock).data[3];

            std::thread readThread([readId, previousValue] {
                // Try starting a write transaction while another read transaction is active.
                auto addRemoveLock = ecs.StartTransaction<Tecs::AddRemove>();

                Assert(readId.GetPrevious<Script>(addRemoveLock).data[3] == previousValue,
                    "Script data should match out read transaction");

                // Commit should not occur since no write operations were done.
                // Test would otherwise block on commit due to active read transaction.
            });
            readThread.join();
        }
    }
    {
        Timer t("Test read with const lock");
        const auto lock = ecs.StartTransaction<Tecs::Read<Transform>>();
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
        }
    }
    {
        Timer t("Test write with const lock");
        const auto lock = ecs.StartTransaction<Tecs::Write<Transform>>();
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            auto &transform = e.Get<Transform>(lock);
            Assert(transform.pos[0] == 1, "Expected position.x to be 1");
        }
    }
    {
        Timer t("Test add/remove with const lock");
        const auto lock = ecs.StartTransaction<Tecs::AddRemove>();
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
        }
    }
    {
        Timer t("Test read lock typecasting");
        auto readLockAll = ecs.StartTransaction<Tecs::Read<Transform, Renderable, Script>>();
        { // Test Subset() method
            auto readLockTransform = readLockAll.Subset<Tecs::Read<Transform>>();
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(e.Get<Transform>(readLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = readLockAll.Subset<Tecs::Read<Script, Renderable>>();
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(readLockScript).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::Lock<ECS, Tecs::Read<Transform>> readLockTransform = readLockAll;
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(e.Get<Transform>(readLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Read<Script, Renderable>> readLockScript = readLockAll;
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(readLockScript).data[0] == 1, "Expected script[0] to be 1");
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
                Assert(e.Get<Transform>(readLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = writeLockAll.Subset<Tecs::Read<Script, Renderable>>();
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(readLockScript).data[0] == 1, "Expected script[0] to be 1");
            }

            auto writeLockTransform = writeLockAll.Subset<Tecs::Write<Transform>>();
            for (Tecs::Entity e : writeLockTransform.EntitiesWith<Transform>()) {
                Assert(e.Get<Transform>(writeLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            auto writeLockScript = writeLockAll.Subset<Tecs::Write<Script, Renderable>>();
            for (Tecs::Entity e : writeLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(writeLockScript).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::Lock<ECS, Tecs::Read<Transform>> readLockTransform = writeLockAll;
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(e.Get<Transform>(readLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Read<Script, Renderable>> readLockScript = writeLockAll;
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(readLockScript).data[0] == 1, "Expected script[0] to be 1");
            }

            Tecs::Lock<ECS, Tecs::Write<Transform>> writeLockTransform = writeLockAll;
            for (Tecs::Entity e : writeLockTransform.EntitiesWith<Transform>()) {
                Assert(e.Get<Transform>(writeLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Write<Script, Renderable>> writeLockScript = writeLockAll;
            for (Tecs::Entity e : writeLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(writeLockScript).data[0] == 1, "Expected script[0] to be 1");
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
                Assert(e.Get<Transform>(readLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            auto readLockScript = writeLockAll.Subset<Tecs::Read<Script, Renderable>>();
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(readLockScript).data[0] == 1, "Expected script[0] to be 1");
            }

            auto writeLockTransform = writeLockAll.Subset<Tecs::Write<Transform>>();
            for (Tecs::Entity e : writeLockTransform.EntitiesWith<Transform>()) {
                Assert(e.Get<Transform>(writeLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            auto writeLockScript = writeLockAll.Subset<Tecs::Write<Script, Renderable>>();
            for (Tecs::Entity e : writeLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(writeLockScript).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        { // Test typecast method
            Tecs::Lock<ECS, Tecs::Read<Transform>> readLockTransform = writeLockAll;
            for (Tecs::Entity e : readLockTransform.EntitiesWith<Transform>()) {
                Assert(e.Get<Transform>(readLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Read<Script, Renderable>> readLockScript = writeLockAll;
            for (Tecs::Entity e : readLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(readLockScript).data[0] == 1, "Expected script[0] to be 1");
            }

            Tecs::Lock<ECS, Tecs::Write<Transform>> writeLockTransform = writeLockAll;
            for (Tecs::Entity e : writeLockTransform.EntitiesWith<Transform>()) {
                Assert(e.Get<Transform>(writeLockTransform).pos[0] == 1, "Expected position.x to be 1");
            }
            Tecs::Lock<ECS, Tecs::Write<Script, Renderable>> writeLockScript = writeLockAll;
            for (Tecs::Entity e : writeLockScript.EntitiesWith<Script>()) {
                Assert(e.Get<Script>(writeLockScript).data[0] == 1, "Expected script[0] to be 1");
            }
        }
        TestReadLock(writeLockAll);
        TestWriteLock(writeLockAll);
        TestAddRemoveLock(writeLockAll);
    }
    {
        Timer t("Test reading observers again");
        auto readLock = ecs.StartTransaction<>();
        Tecs::EntityEvent entityEvent;
        Tecs::ComponentEvent<Transform> transformEvent;
        Tecs::ComponentEvent<GlobalComponent> globalCompEvent;
        Assert(!entityObserver.Poll(readLock, entityEvent), "No events should have occured");
        Assert(!transformObserver.Poll(readLock, transformEvent), "No events should have occured");
        Assert(!globalCompObserver.Poll(readLock, globalCompEvent), "No events should have occured");
    }
    {
        Timer t("Test cross-component write commit");
        std::thread blockingThread;
        { // WriteAll Transaction will commit to all components while another transaction is blocking
            auto writeLock = ecs.StartTransaction<Tecs::WriteAll>();

            for (Tecs::Entity e : writeLock.EntitiesWith<Transform>()) {
                auto &transform = e.Get<Transform>(writeLock);
                transform.pos[1] = transform.pos[0] + 1;
            }
            for (Tecs::Entity e : writeLock.EntitiesWith<Renderable>()) {
                auto &renderable = e.Get<Renderable>(writeLock);
                renderable.name = "foo" + std::to_string(e.id);
            }
            for (Tecs::Entity e : writeLock.EntitiesWith<Script>()) {
                auto &script = e.Get<Script>(writeLock);
                script.data[1] = script.data[0] + 1;
            }

            // Start another write transaction while this one is active to ensure it occurs as soon as possible after
            // the above transaction.
            blockingThread = std::thread([] {
                // Script is the last component in the ECS, and is the first to be commited / unlocked.
                auto lock = ecs.StartTransaction<Tecs::ReadAll, Tecs::Write<Script>>();

                // At this point all components should be commited from the above transaction.
                for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
                    Assert(e.Get<Transform>(lock).pos[1] == 2, "Expected position.y to be 2");
                }
                for (Tecs::Entity e : lock.EntitiesWith<Renderable>()) {
                    Assert(e.Get<Renderable>(lock).name == ("foo" + std::to_string(e.id)),
                        "Expected renderable.name to be foo" + std::to_string(e.id));
                }
                for (Tecs::Entity e : lock.EntitiesWith<Script>()) {
                    Assert(e.Get<Script>(lock).data[1] == 2, "Expected script[1] to be 2");
                }
            });

            // Ensure the blockingThread has time to start before we commit.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        blockingThread.join();
    }
    {
        Timer t("Test continuous overlapping reads");
        std::thread blockingThread;
        std::vector<std::thread> readThreads;
        {
            for (size_t i = 0; i < 10; i++) {
                // Start 10 overlapping read transaction threads
                readThreads.emplace_back([i] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10 * i));
                    auto readLock = ecs.StartTransaction<Tecs::ReadAll>();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                });
            }

            std::atomic_bool commitCompleted = false;
            blockingThread = std::thread([&commitCompleted] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                Tecs::Entity e;
                { // Try to complete an add/remove transaction while continous reads are happening
                    auto lock = ecs.StartTransaction<Tecs::AddRemove>();
                    e = lock.NewEntity();
                    e.Set<Transform>(lock, 1, 2, 3);
                }
                { // Remove the entity we just created
                    auto lock = ecs.StartTransaction<Tecs::AddRemove>();
                    e.Destroy(lock);
                }
                commitCompleted = true;
            });

            while (!commitCompleted) {
                // Cycle through each transaction and restart the thread when it completes
                for (auto &thread : readThreads) {
                    thread.join();
                    thread = std::thread([] {
                        auto readLock = ecs.StartTransaction<Tecs::ReadAll>();
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    });
                }
            }
        }

        blockingThread.join();
        for (auto &thread : readThreads) {
            thread.join();
        }
    }
    {
        Timer t("Test nested transactions");
        try {
            auto lockA = ecs.StartTransaction<Tecs::Read<Transform>, Tecs::Write<Renderable>>();
            auto lockB = ecs.StartTransaction<Tecs::Read<Renderable>, Tecs::Write<Script>>();
            Assert(false, "Nested transactions should not succeed.");
        } catch (std::runtime_error &e) {
            std::string msg = e.what();
            Assert(msg == "Nested transactions are not allowed", "Received wrong runtime_error: " + msg);
        }
    }
    {
        Timer t("Test nested transactions across ecs instances");
        testing::ECS ecs2;
        auto lockA = ecs.StartTransaction<Tecs::Read<Transform>, Tecs::Write<Renderable>>();
        auto lockB = ecs2.StartTransaction<Tecs::Read<Renderable>, Tecs::Write<Script>>();
    }
    {
        Timer t("Test count entities");
        {
            auto readLock = ecs.StartTransaction<>();
            Assert(readLock.Entities().size() == ENTITY_COUNT, "Expected entity count not to change");
        }
    }

    return 0;
}

namespace testing {
    void TestReadLock(Tecs::Lock<ECS, Tecs::Read<Transform>> lock) {
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
        }
    }

    void TestWriteLock(Tecs::Lock<ECS, Tecs::Write<Transform>> lock) {
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
        }
    }

    void TestAddRemoveLock(Tecs::Lock<ECS, Tecs::AddRemove> lock) {
        for (Tecs::Entity e : lock.EntitiesWith<Transform>()) {
            Assert(e.Get<Transform>(lock).pos[0] == 1, "Expected position.x to be 1");
        }
    }
} // namespace testing
