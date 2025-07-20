#include "test_components.hh"
#include "utils.hh"

#include <c_abi/Tecs.hh>
#include <chrono>
#include <cstring>
#include <future>
#include <iomanip>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
#endif

#ifdef TECS_ENABLE_TRACY
    #include <tracy/Tracy.hpp>
#endif

using namespace testing;
using namespace Tecs;

#ifdef BENCHMARK_CABI
TECS_IMPLEMENT_C_ABI
#endif

namespace benchmark {
#ifdef BENCHMARK_CABI
    using AbiECS = Tecs::abi::ECS<Transform, Renderable, Script, GlobalComponent>;
    using Entity = Tecs::abi::Entity;
#endif

    std::atomic_bool running;
    std::atomic_bool success;
#ifdef BENCHMARK_CABI
    static AbiECS ecs = AbiECS();
#else
    static testing::ECS ecs;
#endif

    static std::thread::id renderThreadId;
    static std::thread::id scriptThreadId;
    static std::thread::id transformThreadId;
    static std::thread::id scriptTransactionThreadId;

#ifndef BENCHMARK_CABI
    Observer<testing::ECS, ComponentModifiedEvent<Script>> scriptObserver;
#endif
    std::atomic_size_t scriptUpdateCount;

#define ENTITY_COUNT 1000000
#define ADD_REMOVE_ITERATIONS 100
#define ADD_REMOVE_PER_LOOP 1000
#define SCRIPT_UPDATES_PER_LOOP 50000
#define SCRIPT_THREAD_COUNT 0

#define TRANSFORM_DIVISOR 2
#define RENDERABLE_DIVISOR 3
#define SCRIPT_DIVISOR 5

    void renderThread() {
#ifdef TECS_ENABLE_TRACY
        tracy::SetThreadName("Render");
#endif
        renderThreadId = std::this_thread::get_id();
        MultiTimer timer1("RenderThread StartTransaction");
        MultiTimer timer2("RenderThread Run");
        MultiTimer timer3("RenderThread Unlock");
        std::vector<std::string> bad;
        double currentTransformValue = 0;
        uint32_t currentScriptValue = 0;
        size_t readCount = 0;
        size_t badCount = 0;
        auto start = std::chrono::high_resolution_clock::now();
        auto lastFrameEnd = start;
        while (running) {
            {
                Timer t(timer1);
                auto readLock = ecs.StartTransaction<Read<Renderable, Transform, Script>>();
                t = timer2;

                auto &validRenderables = readLock.EntitiesWith<Renderable>();
                auto &validTransforms = readLock.EntitiesWith<Transform>();
                auto &validEntities =
                    validRenderables.size() > validTransforms.size() ? validTransforms : validRenderables;
                auto firstName = &validEntities[0].Get<Renderable>(readLock).name;
                Entity firstScriptEntity;
                for (auto e : validEntities) {
                    if (e.Has<Renderable, Transform>(readLock)) {
                        if (!firstScriptEntity && e.Has<Script>(readLock)) firstScriptEntity = e;
                        auto &renderable = e.Get<Renderable>(readLock);
                        auto &transform = e.Get<Transform>(readLock);
                        if (transform.pos[0] != transform.pos[1] || transform.pos[1] != transform.pos[2]) {
                            bad.emplace_back(renderable.name);
                        } else {
                            if (&renderable.name == firstName) {
                                currentTransformValue = transform.pos[0];
                            } else if (transform.pos[0] != currentTransformValue) {
                                bad.emplace_back(renderable.name);
                            }
                        }
                    }
                }
                currentScriptValue = firstScriptEntity.Get<Script>(readLock).data[0];

                t = timer3;
            }
#ifdef TECS_ENABLE_TRACY
            FrameMark;
#endif
            readCount++;
            badCount += bad.size();
            bad.clear();
            lastFrameEnd += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) / 90;
            std::this_thread::sleep_until(lastFrameEnd);
        }
        auto delta = std::chrono::high_resolution_clock::now() - start;
        double durationMs = (double)std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
        double avgFrameRate = readCount * 1000 / durationMs;
        if (badCount != 0) {
            std::cerr << "[RenderThread Error] Detected " << badCount << " invalid entities during reading."
                      << std::endl;
        }
        std::cout << "[RenderThread] Average frame rate: " << avgFrameRate << "Hz" << std::endl;
        if (currentScriptValue == 0) {
            double avgUpdateRate = currentTransformValue * 1000 / durationMs;
            std::cout << "[TransformWorkerThread] Average update rate: " << avgUpdateRate << "Hz" << std::endl;
        } else {
            double avgUpdateRate = (double)currentScriptValue * 1000 / durationMs;
            std::cout << "[ScriptTransactionmWorkerThread] Average update rate: " << avgUpdateRate << "Hz" << std::endl;
        }
    }

    void scriptWorkerThread(MultiTimer *workerTimer, Lock<testing::ECS, Write<Script>> lock, EntityView entities) {
        Timer t(*workerTimer);
        for (auto &e : entities) {
            auto &script = e.Get<Script>(lock);
            // "Run" script
            for (uint32_t &data : script.data) {
                data++;
            }
        }
    }

#if SCRIPT_THREAD_COUNT > 0
    void scriptThread() {
    #ifdef TECS_ENABLE_TRACY
        tracy::SetThreadName("Script");
    #endif
        scriptThreadId = std::this_thread::get_id();
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
                    EntityView subspan = entities.subview(workOffset, workPerThread);
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

    void scriptTransactionWorkerThread() {
        scriptTransactionThreadId = std::this_thread::get_id();
        MultiTimer timer1("ScriptTransactionWorkerThread StartTransaction");
        MultiTimer timer2("ScriptTransactionWorkerThread Run");
        MultiTimer timer3("ScriptTransactionWorkerThread Commit");
        size_t iteration = 0;
        while (running) {
            // auto start = std::chrono::high_resolution_clock::now();
            {
                Timer t(timer1);
                auto writeLock = ecs.StartTransaction<Write<Script>>();
                t = timer2;
                auto &validScripts = writeLock.EntitiesWith<Script>();
                size_t modulo = validScripts.size() / SCRIPT_UPDATES_PER_LOOP;
                size_t i = 0;
                for (auto e : validScripts) {
                    if (i % modulo == 0) {
                        auto &script = e.Get<Script>(writeLock);
                        script.data[0]++;
                        script.data[1]++;
                        script.data[2]++;
                    }
                    i++;
                }
                if (iteration % 10 == 9) {
#ifndef BENCHMARK_CABI
                    ComponentModifiedEvent<Script> scriptEventEntity;
                    while (scriptObserver.Poll(writeLock, scriptEventEntity)) {
                        size_t updateNumber = scriptUpdateCount++;
                        size_t expectedEntity =
                            (updateNumber * validScripts.size() / SCRIPT_UPDATES_PER_LOOP) % validScripts.size();
                        if (scriptEventEntity != validScripts[expectedEntity] && success) {
                            std::cerr << "Script modify event #" << (updateNumber + 1) << " "
                                      << std::to_string(scriptEventEntity) << " != expected "
                                      << std::to_string(validScripts[expectedEntity]) << std::endl;
                            success = false;
                        }
                    }
#endif
                }
                t = timer3;
            }
            std::this_thread::yield();
            // std::this_thread::sleep_until(start + std::chrono::milliseconds(11));
            iteration++;
        }
    }

    void transformWorkerThread() {
#ifdef TECS_ENABLE_TRACY
        tracy::SetThreadName("Transform");
#endif
        transformThreadId = std::this_thread::get_id();
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

    template<typename LockType, typename... AllComponentTypes>
    void printComponentCounts(const LockType &lock) {
        ( // For each AllComponentTypes
            [&] {
                std::cout << "  " << ecs.GetComponentName<AllComponentTypes>() << ": ";
                if constexpr (is_global_component<AllComponentTypes>()) {
                    if (lock.template Has<AllComponentTypes>()) {
                        std::cout << "1 global component" << std::endl;
                    } else {
                        std::cout << "no global component" << std::endl;
                    }
                } else {
                    std::cout << lock.template EntitiesWith<AllComponentTypes>().size() << " entities" << std::endl;
                }
            }(),
            ...);
    }

    int runBenchmark() {
#ifdef BENCHMARK_CABI
        std::cout << "Compiled against Tecs C ABI" << std::endl;
#endif
#if __cpp_lib_atomic_wait
        std::cout << "Compiled with C++20 atomic.wait()" << std::endl;
#endif
#ifdef _WIN32
        // Increase thread scheduler from default of 15ms
        timeBeginPeriod(1);
        std::shared_ptr<UINT> timePeriodReset(new UINT(1), [](UINT *period) {
            timeEndPeriod(*period);
            delete period;
        });
#endif
        {
            MultiTimer timer1("Create entities Start");
            MultiTimer timer2("Create entities Run");
            MultiTimer timer3("Create entities Commit");
            Timer t(timer1);
            auto writeLock = ecs.StartTransaction<AddRemove>();
            t = timer2;
            for (size_t i = 0; i < ENTITY_COUNT; i++) {
                Entity e = writeLock.NewEntity();
                if (i % TRANSFORM_DIVISOR == 0) {
                    e.Set<Transform>(writeLock, 0.0, 0.0, 0.0);
                }
                if (i % RENDERABLE_DIVISOR == 0) {
                    e.Set<Renderable>(writeLock, "entity" + std::to_string(i));
                }
                if (i % SCRIPT_DIVISOR == 0) {
                    e.Set<Script>(writeLock, std::initializer_list<uint32_t>({0, 0, 0, 0}));
                }
            }
            t = timer3;
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
                if (removedEntity.components[0]) {
                    e.Set<Transform>(writeLock, 0.0, 0.0, 0.0);
                }
                if (removedEntity.components[1]) {
                    e.Set<Renderable>(writeLock, removedEntity.name);
                }
                if (removedEntity.components[2]) {
                    e.Set<Script>(writeLock, std::initializer_list<uint32_t>({0, 0, 0, 0}));
                }
            }
            t = timer3;
        }
#ifndef BENCHMARK_CABI
        {
            MultiTimer timer1("Watch for script events Start");
            MultiTimer timer2("Watch for script events Run");
            MultiTimer timer3("Watch for script events Commit");
            Timer t(timer1);
            auto writeLock = ecs.StartTransaction<AddRemove>();
            t = timer2;
            scriptObserver = writeLock.Watch<ComponentModifiedEvent<Script>>();
            t = timer3;
        }
#endif

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        ecs.StartTrace();
#endif

        success = true;
        size_t transformCount = 0;
        size_t scriptCount = 0;
        {
            auto readLock = ecs.StartTransaction<>();

            std::cout << "Running with " << readLock.Entities().size() << " Entities and " << ecs.GetComponentCount()
                      << " Component types:" << std::endl;
            printComponentCounts(readLock);

            transformCount = readLock.EntitiesWith<Transform>().size();
            scriptCount = readLock.EntitiesWith<Script>().size();
        }
        {
            Timer t("Run threads (" + std::to_string(transformCount) + " transform updates)");
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

        {
            Timer t("Run threads (" + std::to_string(SCRIPT_UPDATES_PER_LOOP) + " script updates out of " +
                    std::to_string(scriptCount) + ")");
            running = true;

            auto render = std::async(&renderThread);
            auto scriptTransaction = std::async(&scriptTransactionWorkerThread);

            std::this_thread::sleep_for(std::chrono::seconds(10));

            running = false;

            render.wait();
            scriptTransaction.wait();
        }

#ifdef TECS_ENABLE_PERFORMANCE_TRACING
        auto trace = ecs.StopTrace();
        trace.SetThreadName("Main");
        trace.SetThreadName("Render", renderThreadId);
        trace.SetThreadName("Script", scriptThreadId);
        trace.SetThreadName("Transform", transformThreadId);
        trace.SetThreadName("ScriptTransaction", scriptTransactionThreadId);
        trace.SaveToCSV("benchmark-trace.csv");
#endif

#ifndef BENCHMARK_CABI
        {
            MultiTimer timer1("Read script modified events Start");
            MultiTimer timer2("Read script modified events Run");
            MultiTimer timer3("Read script modified events Commit");
            Timer t(timer1);
            auto readLock = ecs.StartTransaction<Read<Script>>();
            t = timer2;
            auto &validScripts = readLock.EntitiesWith<Script>();
            ComponentModifiedEvent<Script> scriptEventEntity;
            while (scriptObserver.Poll(readLock, scriptEventEntity)) {
                size_t updateNumber = scriptUpdateCount++;
                size_t expectedEntity = (updateNumber * scriptCount / SCRIPT_UPDATES_PER_LOOP) % scriptCount;
                if (scriptEventEntity != validScripts[expectedEntity] && success) {
                    std::cerr << "Script modify event #" << (updateNumber + 1) << " "
                              << std::to_string(scriptEventEntity) << " != expected "
                              << std::to_string(validScripts[expectedEntity]) << std::endl;
                    success = false;
                }
            }
            const Entity &lastScript = validScripts[validScripts.size() / SCRIPT_DIVISOR];
            auto &script = lastScript.Get<Script>(readLock);
            size_t iterations = (size_t)script.data[0];
            size_t expectedCount = iterations * SCRIPT_UPDATES_PER_LOOP;
            if (scriptUpdateCount != expectedCount) {
                std::cerr << "Script modify count " << scriptUpdateCount << " != expected " << expectedCount
                          << std::endl;
                success = false;
            }
            t = timer3;
        }

        {
            MultiTimer timer1("Stop watching script events Start");
            MultiTimer timer2("Stop watching script events Run");
            MultiTimer timer3("Stop watching script events Commit");
            Timer t(timer1);
            auto writeLock = ecs.StartTransaction<AddRemove>();
            t = timer2;
            scriptObserver.Stop(writeLock);
            t = timer3;
        }
#endif

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
            double commonValue = 0.0;
            auto readLock = ecs.StartTransaction<Read<Transform>>();
            auto &entityList = readLock.EntitiesWith<Transform>();
            for (auto e : entityList) {
                auto &transform = e.Get<Transform>(readLock);

                if (transform.pos[0] != transform.pos[1] || transform.pos[1] != transform.pos[2]) {
                    if (invalid == 0) {
                        std::cerr << "Component is not in correct place! " << transform.pos[0] << ", "
                                  << transform.pos[1] << ", " << transform.pos[2] << std::endl;
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
            if (invalid != 0) {
                std::cerr << "Error: " << std::to_string(invalid) << " invalid components" << std::endl;
                success = false;
            }
            std::cout << entityList.size() << " total components (" << valid << " with value " << commonValue << ")"
                      << std::endl;
        }

        {
            Timer t("Validate entities std::vector");
            int invalid = 0;
            int valid = 0;
            double commonValue = 0.0;
            for (auto &transform : transforms) {
                if (transform.pos[0] != transform.pos[1] || transform.pos[1] != transform.pos[2]) {
                    if (invalid == 0) {
                        std::cerr << "Component is not in correct place! " << transform.pos[0] << ", "
                                  << transform.pos[1] << ", " << transform.pos[2] << std::endl;
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
            if (invalid != 0) {
                std::cerr << "Error: " << std::to_string(invalid) << " invalid components" << std::endl;
                success = false;
            }
            std::cout << transforms.size() << " total components (" << valid << " with value " << commonValue << ")"
                      << std::endl;
        }

        if (!success) {
            std::cerr << "!!! BENCHMARK FAILED !!!" << std::endl;
        } else {
            std::cout << "Benchmark success" << std::endl;
        }
        return success ? 0 : 1;
    }

} // namespace benchmark

int main(int /* argc */, char ** /* argv */) {
    return benchmark::runBenchmark();
}
