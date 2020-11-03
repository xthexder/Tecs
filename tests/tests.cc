#include "test_components.hh"
#include "utils.hh"

#include <Tecs.hh>
#include <chrono>
#include <cstring>

using namespace testing;

static Tecs::ECS<Transform, Renderable, Script> ecs;

int main(int argc, char **argv) {
    std::cout << "Running with " << ecs.GetComponentCount() << " component types" << std::endl;

    {
        Timer t("Test adding each component type");
        auto writeLock = ecs.AddRemoveEntities();
        for (size_t i = 0; i < 1000; i++) {
            size_t id = writeLock.AddEntity();
            Assert(!writeLock.Has<Transform, Renderable, Script>(id), "Entity must start with no components");

            // Test adding each component type
            Transform value(0.0, 0.0, 0.0, 1);
            writeLock.Set<Transform>(id, value);
            Assert(writeLock.Has<Transform>(id), "Entity should have a Transform component");
            Assert(!writeLock.Has<Renderable, Script>(id), "Entity has extra components");

            writeLock.Set<Renderable>(id, "entity" + std::to_string(i));
            Assert(writeLock.Has<Transform, Renderable>(id), "Entity should have a Transform and Renderable component");
            Assert(!writeLock.Has<Script>(id), "Entity has extra components");

            writeLock.Set<Script>(id, std::initializer_list<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0}));
            Assert(writeLock.Has<Transform, Renderable, Script>(id), "Entity should have all components");

            // Test removing a component
            writeLock.Unset<Renderable>(id);
            Assert(writeLock.Has<Transform, Script>(id), "Entity should have a Transform and Script component");
            Assert(!writeLock.Has<Renderable>(id), "Entity should not have a Renderable component");

            // Test references work after Set()
            auto &script = writeLock.Get<Script>(id);
            Assert(script.data.size() == 8, "Script component should have size 8");
            Assert(script.data[0] == 0, "Script component should have value [(0), 0, 0, 0, 0, 0, 0, 0]");
            Assert(script.data[1] == 0, "Script component should have value [0, (0), 0, 0, 0, 0, 0, 0]");
            Assert(script.data[2] == 0, "Script component should have value [0, 0, (0), 0, 0, 0, 0, 0]");
            Assert(script.data[3] == 0, "Script component should have value [0, 0, 0, (0), 0, 0, 0, 0]");
            Assert(script.data[4] == 0, "Script component should have value [0, 0, 0, 0, (0), 0, 0, 0]");
            Assert(script.data[5] == 0, "Script component should have value [0, 0, 0, 0, 0, (0), 0, 0]");
            Assert(script.data[6] == 0, "Script component should have value [0, 0, 0, 0, 0, 0, (0), 0]");
            Assert(script.data[7] == 0, "Script component should have value [0, 0, 0, 0, 0, 0, 0, (0)]");

            writeLock.Set<Script>(id, std::initializer_list<uint8_t>({1, 2, 3, 4}));
            Assert(writeLock.Has<Transform, Script>(id), "Entity should have a Transform and Script component");
            Assert(!writeLock.Has<Renderable>(id), "Entity should not have a Renderable component");

            Assert(script.data.size() == 4, "Script component should have size 4");
            Assert(script.data[0] == 1, "Script component should have value [(1), 2, 3, 4]");
            Assert(script.data[1] == 2, "Script component should have value [1, (2), 3, 4]");
            Assert(script.data[2] == 3, "Script component should have value [1, 2, (3), 4]");
            Assert(script.data[3] == 4, "Script component should have value [1, 2, 3, (4)]");
        }
    }

    return 0;
}
