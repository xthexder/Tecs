#include "test_components.hh"
#include "test_ecs.hh"
#include "tests.hh"
#include "utils.hh"

#include <Tecs.hh>
#include <c_abi/Tecs.hh>
#include <c_abi/Tecs_entity.hh>
#include <c_abi/Tecs_lock.hh>
#include <iostream>

using namespace testing;

using AbiECS = Tecs::abi::ECS<Transform, Renderable, Script, GlobalComponent>;

static ECS baseEcs;

#define ENTITY_COUNT 10000

int main(int /* argc */, char ** /* argv */) {
    std::shared_ptr<TecsECS> ecsPtr(&baseEcs, [](auto *) {});
    AbiECS ecs = AbiECS(ecsPtr);

    std::cout << "Running with " << ENTITY_COUNT << " entities and " << ecs.GetComponentCount() << " component types"
              << std::endl;
    std::cout << ecs.GetBytesPerEntity() << " bytes per entity * N = " << (ecs.GetBytesPerEntity() * ENTITY_COUNT)
              << " bytes total" << std::endl;
    std::cout << "Using C ABI" << std::endl;

    return 0;
}
