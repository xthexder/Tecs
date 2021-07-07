#include "test_components.hh"
#include "utils.hh"

#include <Tecs.hh>
#include <chrono>
#include <cstring>
#include <future>
#include <iomanip>
#include <thread>

using namespace testing;
using namespace Tecs;

std::atomic_bool running;
static testing::ECS ecs;

#define ENTITY_COUNT 1000000
#define ADD_REMOVE_ITERATIONS 100
#define ADD_REMOVE_PER_LOOP 1000
#define SCRIPT_THREAD_COUNT 0

#define TRANSFORM_DIVISOR 2
#define RENDERABLE_DIVISOR 3
#define SCRIPT_DIVISOR 10

void renderThread() {
    MultiTimer timer1("RenderThread StartTransaction");
    MultiTimer timer2("RenderThread Run");
    MultiTimer timer3("RenderThread Unlock");
    std::vector<std::string> bad;
    double currentValue = 0;
    size_t readCount = 0;
    size_t badCount = 0;
    auto start = std::chrono::high_resolution_clock::now();
    auto lastFrameEnd = start;
    while (running) {
        {
            Timer t(timer1);
            auto readLock = ecs.StartTransaction<Read<Renderable, Transform>>();
            t = timer2;

            auto &validRenderables = readLock.EntitiesWith<Renderable>();
            auto &validTransforms = readLock.EntitiesWith<Transform>();
            auto &validEntities = validRenderables.size() > validTransforms.size() ? validTransforms : validRenderables;
            auto firstName = &validEntities[0].Get<Renderable>(readLock).name;
            for (auto e : validEntities) {
                if (e.Has<Renderable, Transform>(readLock)) {
                    auto &renderable = e.Get<Renderable>(readLock);
                    auto &transform = e.Get<Transform>(readLock);
                    if (transform.pos[0] != transform.pos[1] || transform.pos[1] != transform.pos[2]) {
                        bad.emplace_back(renderable.name);
                    } else {
                        if (&renderable.name == firstName) {
                            currentValue = transform.pos[0];
                        } else if (transform.pos[0] != currentValue) {
                            bad.emplace_back(renderable.name);
                        }
                    }
                }
            }

            t = timer3;
        }
        readCount++;
        badCount += bad.size();
        bad.clear();
        lastFrameEnd += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) / 90;
        std::this_thread::sleep_until(lastFrameEnd);
    }
    auto delta = std::chrono::high_resolution_clock::now() - start;
    double durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
    double avgFrameRate = readCount * 1000 / (double)durationMs;
    double avgUpdateRate = currentValue * 1000 / (double)durationMs;
    if (badCount != 0) {
        std::cerr << "[RenderThread Error] Detected " << badCount << " invalid entities during reading." << std::endl;
    }
    std::cout << "[RenderThread] Average frame rate: " << avgFrameRate << "Hz" << std::endl;
    std::cout << "[TransformWorkerThread] Average update rate: " << avgUpdateRate << "Hz" << std::endl;
}

void scriptWorkerThread(MultiTimer *workerTimer, Lock<testing::ECS, Write<Script>> lock,
    nonstd::span<Entity> entities) {
    Timer t(*workerTimer);
    for (auto &e : entities) {
        auto &script = e.Get<Script>(lock);
        // "Run" script
        for (uint8_t &data : script.data) {
            data++;
        }
    }
}

#if SCRIPT_THREAD_COUNT > 0
void scriptThread() {
    MultiTimer timer1("ScriptThread StartTransaction");
    MultiTimer timer2("ScriptThread Run");
    MultiTimer timer3("ScriptThread Unlock");
    MultiTimer workerTimers[SCRIPT_THREAD_COUNT];
    std::future<void> workers[SCRIPT_THREAD_COUNT];
    for (size_t i = 0; i < SCRIPT_THREAD_COUNT; i++) {
        workerTimers[i].Reset("ScriptWorker " + std::to_string(i) + " Run");
    }
    auto start = std::chrono::high_resolution_clock::now();
    auto lastFrameEnd = start;
    while (running) {
        {
            Timer t(timer1);
            auto lock = ecs.StartTransaction<Write<Script>>();
            t = timer2;
            auto &entities = lock.PreviousEntitiesWith<Script>();
            size_t workPerThread = entities.size() / SCRIPT_THREAD_COUNT;
            size_t workOffset = 0;
            for (size_t i = 0; i < SCRIPT_THREAD_COUNT; i++) {
                nonstd::span<Entity> subspan =
                    entities.subspan(workOffset, std::min(workPerThread, entities.size() - workOffset));
                workers[i] = std::async(&scriptWorkerThread, &workerTimers[i], lock, subspan);
                workOffset += workPerThread;
            }
            for (auto &worker : workers) {
                worker.wait();
            }
            t = timer3;
        }
        lastFrameEnd += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) / 60;
        std::this_thread::sleep_until(lastFrameEnd);
    }
}
#endif

void transformWorkerThread() {
    MultiTimer timer1("TransformWorkerThread StartTransaction");
    MultiTimer timer2("TransformWorkerThread Run");
    MultiTimer timer3("TransformWorkerThread Commit");
    while (running) {
        // auto start = std::chrono::high_resolution_clock::now();
        {
            Timer t(timer1);
            auto writeLock = ecs.StartTransaction<Write<Transform>>();
            t = timer2;
            auto &validTransforms = writeLock.EntitiesWith<Transform>();
            for (auto e : validTransforms) {
                auto &transform = e.Get<Transform>(writeLock);
                transform.pos[0]++;
                transform.pos[1]++;
                transform.pos[2]++;
            }
            t = timer3;
        }
        std::this_thread::yield();
        // std::this_thread::sleep_until(start + std::chrono::milliseconds(11));
    }
}

int main(int argc, char **argv) {
    {
        Timer t("Create entities");
        auto writeLock = ecs.StartTransaction<AddRemove>();
        for (size_t i = 0; i < ENTITY_COUNT; i++) {
            Entity e = writeLock.NewEntity();
            if (i % TRANSFORM_DIVISOR == 0) { e.Set<Transform>(writeLock, 0.0, 0.0, 0.0, 1); }
            if (i % RENDERABLE_DIVISOR == 0) { e.Set<Renderable>(writeLock, "entity" + std::to_string(i)); }
            if (i % SCRIPT_DIVISOR == 0) {
                e.Set<Script>(writeLock, std::initializer_list<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0}));
            }
        }
    }

    struct RemovedEntity {
        std::string name;
        std::bitset<3> components;
    };
    std::vector<RemovedEntity> removedList;
    {
        MultiTimer timer1("Remove the first " + std::to_string(ADD_REMOVE_PER_LOOP) + " entities x" +
                          std::to_string(ADD_REMOVE_ITERATIONS) + " Start");
        MultiTimer timer2("Remove the first " + std::to_string(ADD_REMOVE_PER_LOOP) + " entities x" +
                          std::to_string(ADD_REMOVE_ITERATIONS) + " Run");
        MultiTimer timer3("Remove the first " + std::to_string(ADD_REMOVE_PER_LOOP) + " entities x" +
                          std::to_string(ADD_REMOVE_ITERATIONS) + " Commit");
        for (size_t i = 0; i < ADD_REMOVE_ITERATIONS; i++) {
            Timer t(timer1);
            auto writeLock = ecs.StartTransaction<AddRemove>();
            t = timer2;
            auto &entities = writeLock.Entities();
            for (size_t j = 0; j < ADD_REMOVE_PER_LOOP; j++) {
                Entity e = entities[j];

                auto &removedEntity = removedList.emplace_back();
                removedEntity.components[0] = e.Has<Transform>(writeLock);
                if (e.Has<Renderable>(writeLock)) {
                    removedEntity.name = e.Get<Renderable>(writeLock).name;
                    removedEntity.components[1] = true;
                }
                removedEntity.components[2] = e.Has<Script>(writeLock);
                e.Destroy(writeLock);
            }
            t = timer3;
        }
    }

    {
        MultiTimer timer1("Recreate removed entities Start");
        MultiTimer timer2("Recreate removed entities Run");
        MultiTimer timer3("Recreate removed entities Commit");
        Timer t(timer1);
        auto writeLock = ecs.StartTransaction<AddRemove>();
        t = timer2;
        for (auto removedEntity : removedList) {
            Entity e = writeLock.NewEntity();
            if (removedEntity.components[0]) { e.Set<Transform>(writeLock, 0.0, 0.0, 0.0, 1); }
            if (removedEntity.components[1]) { e.Set<Renderable>(writeLock, removedEntity.name); }
            if (removedEntity.components[2]) {
                e.Set<Script>(writeLock, std::initializer_list<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0}));
            }
        }
        t = timer3;
    }

    return 0;

    {
        {
            auto readLock = ecs.StartTransaction<>();
            std::cout << "Running with " << readLock.Entities().size() << " Entities and " << ecs.GetComponentCount()
                      << " Component types" << std::endl;
        }

        Timer t("Run threads");
        running = true;

        auto render = std::async(&renderThread);
        auto transform = std::async(&transformWorkerThread);
#if SCRIPT_THREAD_COUNT > 0
        auto script = std::async(&scriptThread);
#endif

        std::this_thread::sleep_for(std::chrono::seconds(10));

        running = false;

        render.wait();
        transform.wait();
#if SCRIPT_THREAD_COUNT > 0
        script.wait();
#endif
    }

    std::vector<Transform> transforms;
    {
        Timer t("Copy entities to std::vector");
        auto readLock = ecs.StartTransaction<Read<Transform>>();
        auto &entityList = readLock.EntitiesWith<Transform>();
        transforms.resize(entityList.size());
        for (size_t i = 0; i < entityList.size(); i++) {
            transforms[i] = entityList[i].Get<Transform>(readLock);
        }
    }

    {
        Timer t("Validate entities Tecs");
        int invalid = 0;
        int valid = 0;
        double commonValue;
        auto readLock = ecs.StartTransaction<Read<Transform>>();
        auto &entityList = readLock.EntitiesWith<Transform>();
        for (auto e : entityList) {
            auto &transform = e.Get<Transform>(readLock);

            if (transform.pos[0] != transform.pos[1] || transform.pos[1] != transform.pos[2]) {
                if (invalid == 0) {
                    std::cerr << "Component is not in correct place! " << transform.pos[0] << ", " << transform.pos[1]
                              << ", " << transform.pos[2] << std::endl;
                }
                invalid++;
            } else {
                if (valid == 0) {
                    commonValue = transform.pos[0];
                } else if (transform.pos[0] != commonValue) {
                    if (invalid == 0) {
                        std::cerr << "Component is not in correct place! " << transform.pos[0] << ", "
                                  << transform.pos[1] << ", " << transform.pos[2] << std::endl;
                    }
                    invalid++;
                }
                valid++;
            }
        }
        if (invalid != 0) { std::cerr << "Error: " << std::to_string(invalid) << " invalid components" << std::endl; }
        std::cout << entityList.size() << " total components (" << valid << " with value " << commonValue << ")"
                  << std::endl;
    }

    {
        Timer t("Validate entities std::vector");
        int invalid = 0;
        int valid = 0;
        double commonValue;
        for (auto &transform : transforms) {
            if (transform.pos[0] != transform.pos[1] || transform.pos[1] != transform.pos[2]) {
                if (invalid == 0) {
                    std::cerr << "Component is not in correct place! " << transform.pos[0] << ", " << transform.pos[1]
                              << ", " << transform.pos[2] << std::endl;
                }
                invalid++;
            } else {
                if (valid == 0) {
                    commonValue = transform.pos[0];
                } else if (transform.pos[0] != commonValue) {
                    if (invalid == 0) {
                        std::cerr << "Component is not in correct place! " << transform.pos[0] << ", "
                                  << transform.pos[1] << ", " << transform.pos[2] << std::endl;
                    }
                    invalid++;
                }
                valid++;
            }
        }
        if (invalid != 0) { std::cerr << "Error: " << std::to_string(invalid) << " invalid components" << std::endl; }
        std::cout << transforms.size() << " total components (" << valid << " with value " << commonValue << ")"
                  << std::endl;
    }

    return 0;
}
