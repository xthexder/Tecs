#include "test_components.hh"
#include "test_ecs.hh"
#include "tests.hh"
#include "utils.hh"

#include <Tecs.hh>
#include <c_abi/Tecs_entity.h>
#include <c_abi/Tecs_lock.h>
#include <iostream>

using namespace testing;

static ECS ecs;

#define ENTITY_COUNT 10000

int main(int /* argc */, char ** /* argv */) {
    std::cout << "Running with " << ENTITY_COUNT << " entities and " << ecs.GetComponentCount() << " component types"
              << std::endl;
    std::cout << ecs.GetBytesPerEntity() << " bytes per entity * N = " << (ecs.GetBytesPerEntity() * ENTITY_COUNT)
              << " bytes total" << std::endl;
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
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::DynamicLock<ECS> dynLock = writeLock;
        Assert(!Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()),
            "ECS must start with no global component");
        GlobalComponent tmp(0);
        GlobalComponent *gc =
            static_cast<GlobalComponent *>(Tecs_set(&dynLock, ECS::GetComponentIndex<GlobalComponent>(), &tmp));
        Assert(gc, "ECS should have returned global component");
        Assert(Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()), "ECS should have a global component");
        Assert(gc->globalCounter == 0, "Global counter should be initialized to zero");
        gc->globalCounter++;

        globalComponentInitialized = true;
        gc->test = std::shared_ptr<bool>(&globalComponentInitialized, [](bool *b) {
            *b = false;
        });

        Assert(!Tecs_had(&dynLock, ECS::GetComponentIndex<GlobalComponent>()),
            "ECS shouldn't have a global component previously");
        const GlobalComponent *gcconst =
            static_cast<const GlobalComponent *>(Tecs_const_get(&dynLock, ECS::GetComponentIndex<GlobalComponent>()));
        Assert(gcconst->globalCounter == 1, "Expected to be able to read const global counter");

        GlobalComponent *gc2 =
            static_cast<GlobalComponent *>(Tecs_get(&dynLock, ECS::GetComponentIndex<GlobalComponent>()));
        Assert(gc2->globalCounter == 1, "Global counter should be read back as 1");
        Assert(globalComponentInitialized, "Global component should be initialized");

        Assert(Tecs_lock_get_transaction_id(&dynLock) == 2, "Expected transaction id to be 2");
    }
    {
        Timer t("Test update global counter");
        auto writeLock = ecs.StartTransaction<Tecs::Write<GlobalComponent>>();
        Tecs::DynamicLock<ECS> dynLock = writeLock;
        Assert(Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()), "ECS should have a global component");

        GlobalComponent *gc =
            static_cast<GlobalComponent *>(Tecs_get(&dynLock, ECS::GetComponentIndex<GlobalComponent>()));
        Assert(gc->globalCounter == 1, "Global counter should be read back as 1");
        gc->globalCounter++;

        Assert(Tecs_had(&dynLock, ECS::GetComponentIndex<GlobalComponent>()),
            "ECS shouldn have a global component previously");
        Assert(
            static_cast<const GlobalComponent *>(Tecs_get_previous(&dynLock, ECS::GetComponentIndex<GlobalComponent>()))
                    ->globalCounter == 1,
            "Expected previous counter to be 1");
        Assert(static_cast<GlobalComponent *>(Tecs_get(&dynLock, ECS::GetComponentIndex<GlobalComponent>()))
                       ->globalCounter == 2,
            "Expected current counter to be 2");
        Assert(static_cast<const GlobalComponent *>(Tecs_const_get(&dynLock, ECS::GetComponentIndex<GlobalComponent>()))
                       ->globalCounter == 2,
            "Expected const current counter to be 2");

        Assert(globalComponentInitialized, "Global component should be initialized");
    }
    {
        Timer t("Test read global counter");
        auto readLock = ecs.StartTransaction<Tecs::Read<GlobalComponent>>();
        Tecs::DynamicLock<ECS> dynLock = readLock;
        Assert(Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()), "ECS should have a global component");

        const GlobalComponent *gc =
            static_cast<const GlobalComponent *>(Tecs_const_get(&dynLock, ECS::GetComponentIndex<GlobalComponent>()));
        Assert(gc->globalCounter == 2, "Global counter should be read back as 2");
    }
    {
        Timer t("Test remove global component");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::DynamicLock<ECS> dynLock = writeLock;
        Assert(Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()), "ECS should have a global component");

        GlobalComponent *gc =
            static_cast<GlobalComponent *>(Tecs_get(&dynLock, ECS::GetComponentIndex<GlobalComponent>()));
        Assert(gc->globalCounter == 2, "Global counter should be read back as 2");

        Tecs_unset(&dynLock, ECS::GetComponentIndex<GlobalComponent>());
        Assert(!Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()), "Global component should be removed");
        Assert(Tecs_had(&dynLock, ECS::GetComponentIndex<GlobalComponent>()), "ECS should still know previous state");
        Assert(globalComponentInitialized, "Global component should still be initialized (kept by read pointer)");
    }
    Assert(globalComponentInitialized, "Global component should still be initialized (kept by observer)");
    {
        Timer t("Test add remove global component in single transaction");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::DynamicLock<ECS> dynLock = writeLock;
        Assert(!Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()), "Global component should be removed");

        GlobalComponent *gc =
            static_cast<GlobalComponent *>(Tecs_get(&dynLock, ECS::GetComponentIndex<GlobalComponent>()));
        Assert(Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()),
            "Get call should have initialized global component");
        Assert(gc->globalCounter == 10, "Global counter should be default initialized to 10");

        bool compInitialized = true;
        gc->test = std::shared_ptr<bool>(&compInitialized, [](bool *b) {
            *b = false;
        });

        // Try removing the component in the same transaction it was created
        Tecs_unset(&dynLock, ECS::GetComponentIndex<GlobalComponent>());
        Assert(!Tecs_has(&dynLock, ECS::GetComponentIndex<GlobalComponent>()), "Global component should be removed");

        Assert(!compInitialized, "Global component should be deconstructed immediately");
    }
    {
        Timer t("Test simple add entity");
        auto writeLock = ecs.StartTransaction<Tecs::AddRemove>();
        Tecs::DynamicLock<ECS> dynLock = writeLock;
        for (size_t i = 0; i < 100; i++) {
            Tecs::Entity e = Tecs_new_entity(&dynLock);
            Assert(e.index == i,
                "Expected Nth entity index to be " + std::to_string(ENTITY_COUNT + i) + ", was " + std::to_string(e));
            Assert(Tecs::GenerationWithoutIdentifier(e.generation) == 1,
                "Expected Nth entity generation to be 1, was " + std::to_string(e));
            Assert(Tecs::IdentifierFromGeneration(e.generation) == 1,
                "Expected Nth entity ecsId to be 1, was " + std::to_string(e));

            Transform tmpTransform(0.0, 0.0, 0.0);
            Tecs_entity_set(&dynLock, (TecsEntity)e, ECS::GetComponentIndex<Transform>(), &tmpTransform);
            Script tmpScript(std::initializer_list<uint8_t>({1, 2, 3, 4}));
            Tecs_entity_set(&dynLock, (TecsEntity)e, ECS::GetComponentIndex<Script>(), &tmpScript);
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
