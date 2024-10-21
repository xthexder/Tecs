#include "test_components.hh"
#include "test_ecs.hh"
#include "tests.hh"
#include "utils.hh"

#include <Tecs.hh>
#include <c_abi/Tecs.h>
#include <c_abi/Tecs_entity.h>
#include <c_abi/Tecs_entity.hh>
#include <c_abi/Tecs_lock.h>
#include <c_abi/Tecs_lock.hh>
#include <iostream>

using namespace testing;

static ECS ecs;

#define ENTITY_COUNT 10000

int main(int /* argc */, char ** /* argv */) {
    size_t componentCount = Tecs_ecs_get_component_count(&ecs);
    size_t bytesPerEntity = Tecs_ecs_get_bytes_per_entity(&ecs);
    std::cout << "Running with " << ENTITY_COUNT << " entities and " << componentCount << " component types"
              << std::endl;
    std::cout << bytesPerEntity << " bytes per entity * N = " << (bytesPerEntity * ENTITY_COUNT) << " bytes total"
              << std::endl;
    std::cout << "Using C ABI" << std::endl;

    Assert(Tecs::nextTransactionId == 0, "Expected next transaction id to be 0");

    Tecs::Observer<ECS, Tecs::EntityEvent> entityObserver;
    Tecs::Observer<ECS, Tecs::ComponentEvent<Transform>> transformObserver;
    Tecs::Observer<ECS, Tecs::ComponentEvent<GlobalComponent>> globalCompObserver;
    {
        Timer t("Test creating new observers");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::DynamicLock<ECS> dynLock = writeLock;
        entityObserver = writeLock.Watch<Tecs::EntityEvent>();
        transformObserver = writeLock.Watch<Tecs::ComponentEvent<Transform>>();
        globalCompObserver = writeLock.Watch<Tecs::ComponentEvent<GlobalComponent>>();

        Assert(Tecs_lock_get_transaction_id(&dynLock) == 1, "Expected transaction id to be 1");
    }
    Assert(Tecs::nextTransactionId == 1, "Expected next transaction id to be 1");
    bool globalComponentInitialized = false;
    {
        Timer t("Test initializing global components");
        Tecs::DynamicLock<ECS> dynLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::abi::Lock<ECS, Tecs::AddRemove> writeLock(std::shared_ptr<TecsLock>(&dynLock, [](auto *) {}));
        Assert(!writeLock.Has<GlobalComponent>(), "ECS must start with no global component");
        auto &gc = writeLock.Set<GlobalComponent>(0);
        Assert(writeLock.Has<GlobalComponent>(), "ECS should have a global component");
        Assert(gc.globalCounter == 0, "Global counter should be initialized to zero");
        gc.globalCounter++;

        globalComponentInitialized = true;
        gc.test = std::shared_ptr<bool>(&globalComponentInitialized, [](bool *b) {
            *b = false;
        });

        Assert(!writeLock.Had<GlobalComponent>(), "ECS shouldn't have a global component previously");
        Assert(writeLock.Get<const GlobalComponent>().globalCounter == 1,
            "Expected to be able to read const global counter");

        auto &gc2 = writeLock.Get<GlobalComponent>();
        Assert(gc2.globalCounter == 1, "Global counter should be read back as 1");
        Assert(globalComponentInitialized, "Global component should be initialized");

        Assert(writeLock.GetTransactionId() == 2, "Expected transaction id to be 2");
    }
    {
        Timer t("Test update global counter");
        Tecs::DynamicLock<ECS> dynLock = ecs.StartTransaction<Tecs::Write<GlobalComponent>>();
        Tecs::abi::Lock<ECS, Tecs::Write<GlobalComponent>> writeLock(
            std::shared_ptr<TecsLock>(&dynLock, [](auto *) {}));
        Assert(writeLock.Has<GlobalComponent>(), "ECS should have a global component");

        auto &gc = writeLock.Get<GlobalComponent>();
        Assert(gc.globalCounter == 1, "Global counter should be read back as 1");
        gc.globalCounter++;

        Assert(writeLock.Had<GlobalComponent>(), "ECS shouldn have a global component previously");
        Assert(writeLock.GetPrevious<GlobalComponent>().globalCounter == 1, "Expected previous counter to be 1");
        Assert(writeLock.GetPrevious<const GlobalComponent>().globalCounter == 1,
            "Expected const previous counter to be 1");
        Assert(writeLock.Get<GlobalComponent>().globalCounter == 2, "Expected current counter to be 2");
        Assert(writeLock.Get<const GlobalComponent>().globalCounter == 2, "Expected const current counter to be 2");

        Assert(globalComponentInitialized, "Global component should be initialized");
    }
    {
        Timer t("Test read global counter");
        Tecs::DynamicLock<ECS> dynLock = ecs.StartTransaction<Tecs::Read<GlobalComponent>>();
        Tecs::abi::Lock<ECS, Tecs::Read<GlobalComponent>> readLock(std::shared_ptr<TecsLock>(&dynLock, [](auto *) {}));
        Assert(readLock.Has<GlobalComponent>(), "ECS should have a global component");

        auto &gc = readLock.Get<GlobalComponent>();
        Assert(gc.globalCounter == 2, "Global counter should be read back as 2");
    }
    {
        Timer t("Test remove global component");
        Tecs::DynamicLock<ECS> dynLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::abi::Lock<ECS, Tecs::AddRemove> writeLock(std::shared_ptr<TecsLock>(&dynLock, [](auto *) {}));
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
        Tecs::DynamicLock<ECS> dynLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::abi::Lock<ECS, Tecs::AddRemove> writeLock(std::shared_ptr<TecsLock>(&dynLock, [](auto *) {}));
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
        Timer t("Test simple add entity");
        Tecs::DynamicLock<ECS> dynLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::abi::Lock<ECS, Tecs::AddRemove> writeLock(std::shared_ptr<TecsLock>(&dynLock, [](auto *) {}));
        for (size_t i = 0; i < 100; i++) {
            Tecs::abi::Entity e = writeLock.NewEntity();
            Assert(e.index == i,
                "Expected Nth entity index to be " + std::to_string(ENTITY_COUNT + i) + ", was " + std::to_string(e));
            Assert(Tecs::GenerationWithoutIdentifier(e.generation) == 1,
                "Expected Nth entity generation to be 1, was " + std::to_string(e));
            Assert(Tecs::IdentifierFromGeneration(e.generation) == 1,
                "Expected Nth entity ecsId to be 1, was " + std::to_string(e));

            e.Set<Transform>(writeLock, 0.0, 0.0, 0.0);
            e.Set<Script>(writeLock, std::initializer_list<uint8_t>({1, 2, 3, 4}));
            AssertHas<Transform, Script>(writeLock, e);
        }
    }
    {
        Timer t("Test stopping observers");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        entityObserver.Stop(writeLock);
        transformObserver.Stop(writeLock);
        globalCompObserver.Stop(writeLock);
    }

    std::cout << "Tests succeeded" << std::endl;
    return 0;
}
