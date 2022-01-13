#include "test_components.hh"
#include "utils.hh"

#include <Tecs.hh>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
#endif

using namespace testing;
using namespace Tecs;

#define ITERATIONS 1000
#define ENTITY_COUNT 10000

int main(int /* argc */, char ** /* argv */) {
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
        MultiTimer timer1("Simple insert");
        for (size_t x = 0; x < ITERATIONS; x++) {
            Timer t(timer1);
            testing::ECS ecs;

            auto writeLock = ecs.StartTransaction<AddRemove>();
            for (size_t i = 0; i < ENTITY_COUNT; i++) {
                Entity e = writeLock.NewEntity();
                e.Set<Transform>(writeLock);
                e.Set<Position>(writeLock);
                e.Set<Rotation>(writeLock);
                e.Set<Velocity>(writeLock);
            }
        }
    }
    testing::ECS ecs1;
    {
        auto writeLock = ecs1.StartTransaction<AddRemove>();

        for (size_t i = 0; i < ENTITY_COUNT; i++) {
            Entity e = writeLock.NewEntity();
            e.Set<Transform>(writeLock);
            e.Set<Position>(writeLock);
            e.Set<Rotation>(writeLock);
            e.Set<Velocity>(writeLock);
        }
    }
    {
        MultiTimer timer1("Simple iter");
        for (size_t x = 0; x < ITERATIONS; x++) {
            Timer t(timer1);
            auto writeLock = ecs1.StartTransaction<Read<Velocity>, Write<Position>>();
            for (auto &e : writeLock.EntitiesWith<Velocity>()) {
                // if (!e.Has<Position, Velocity>(writeLock)) continue;

                auto &pos = e.Get<Position>(writeLock).pos;
                auto &vel = e.Get<Velocity>(writeLock).vel;
                pos[0] += vel[0];
                pos[1] += vel[1];
                pos[2] += vel[2];
            }
        }
    }

    testing::ECS2 ecs2;
    {
        auto writeLock = ecs2.StartTransaction<AddRemove>();

        std::tuple<A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z> foo;
        std::apply(
            [&writeLock](auto &&...args) {
                (MakeEntities<decltype(args)>(), ...);
            },
            foo);
    }
    {
        MultiTimer timer1("Fragmented iter");
        for (size_t x = 0; x < ITERATIONS; x++) {
            Timer t(timer1);
            auto writeLock = ecs2.StartTransaction<Write<Data>>();
            for (auto &e : writeLock.EntitiesWith<Data>()) {
                e.Get<Data>(writeLock).v *= 2;
            }
        }
    }

    testing::ECS2 ecs3;
    {
        auto writeLock = ecs3.StartTransaction<AddRemove>();

        for (size_t x = 0; x < ENTITY_COUNT; x++) {
            Entity e = writeLock.NewEntity();
            e.Set<A>(writeLock);
            e.Set<B>(writeLock);
        }
        for (size_t x = 0; x < ENTITY_COUNT; x++) {
            Entity e = writeLock.NewEntity();
            e.Set<A>(writeLock);
            e.Set<B>(writeLock);
            e.Set<C>(writeLock);
        }
        for (size_t x = 0; x < ENTITY_COUNT; x++) {
            Entity e = writeLock.NewEntity();
            e.Set<A>(writeLock);
            e.Set<B>(writeLock);
            e.Set<C>(writeLock);
            e.Set<D>(writeLock);
        }
        for (size_t x = 0; x < ENTITY_COUNT; x++) {
            Entity e = writeLock.NewEntity();
            e.Set<A>(writeLock);
            e.Set<B>(writeLock);
            e.Set<C>(writeLock);
            e.Set<E>(writeLock);
        }
    }
    {
        Timer timer("System scheduling");
        MultiTimer timer1("System scheduling AB");
        MultiTimer timer2("System scheduling CD");
        MultiTimer timer3("System scheduling CE");
        std::thread ab([&ecs3, &timer1] {
            for (size_t x = 0; x < ITERATIONS; x++) {
                Timer t(timer1);
                auto writeLock = ecs3.StartTransaction<Write<A, B>>();
                for (auto &e : writeLock.EntitiesWith<B>()) {
                    std::swap(e.Get<A>(writeLock).v, e.Get<B>(writeLock).v);
                }
            }
        });
        std::thread cd([&ecs3, &timer2] {
            for (size_t x = 0; x < ITERATIONS; x++) {
                Timer t(timer2);
                auto writeLock = ecs3.StartTransaction<Write<C, D>>();
                for (auto &e : writeLock.EntitiesWith<D>()) {
                    std::swap(e.Get<C>(writeLock).v, e.Get<D>(writeLock).v);
                }
            }
        });
        std::thread ce([&ecs3, &timer3] {
            for (size_t x = 0; x < ITERATIONS; x++) {
                Timer t(timer3);
                auto writeLock = ecs3.StartTransaction<Write<C, E>>();
                for (auto &e : writeLock.EntitiesWith<E>()) {
                    std::swap(e.Get<C>(writeLock).v, e.Get<E>(writeLock).v);
                }
            }
        });
        ab.join();
        cd.join();
        ce.join();
    }

    return 0;
}
