#include "complex_component.hh"
#include "components.hh"

#include <fstream>
#include <iostream>
#include <vector>

using namespace example;
using namespace Tecs;

// Instantiate an ECS using our defined World
static World ecs;

int main(int /* argc */, char ** /* argv */) {
    ecs.StartTrace();

    { // Setup entity state for the example
        auto lock = ecs.StartTransaction<AddRemove>();

        for (int i = 0; i < 100; i++) {
            Tecs::Entity e = lock.NewEntity();

            if (i < 10) e.Set<Name>(lock, std::to_string(i));
            e.Set<Position>(lock);
            e.Set<State>(lock, (State)(i % STATE_COUNT));
            e.Set<ComplexComponent>(lock);
        }
    }

    // Start 3 threads with overlapping transaction permissions
    std::atomic_bool running = true;
    std::vector<std::thread> threads;
    threads.emplace_back([&running] {
        while (running) {
            { // Print positions and increment ComplexComponent
                auto lock = ecs.StartTransaction<Read<Name, Position>, Write<ComplexComponent>>();

                std::cout << "Update:" << std::endl;
                for (auto e : lock.EntitiesWith<Name>()) {
                    if (e.Has<Name, Position>(lock)) {
                        auto &name = e.Get<Name>(lock);
                        auto &pos = e.Get<Position>(lock);

                        std::cout << "    Entity: " << name << " at (" << pos.x << ", " << pos.y << ")" << std::endl;
                    }
                }

                for (auto e : lock.EntitiesWith<ComplexComponent>()) {
                    auto &comp = e.Get<ComplexComponent>(lock);
                    comp.Set(comp.Get() + 1);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    });
    threads.emplace_back([&running] {
        while (running) {
            { // Read State and update Position accordingly
                auto lock = ecs.StartTransaction<Read<State>, Write<Position>>();

                for (auto e : lock.EntitiesWith<State>()) {
                    if (e.Has<State, Position>(lock)) {
                        auto &state = e.Get<State>(lock);
                        auto &pos = e.Get<Position>(lock);
                        switch (state) {
                        case MOVING_LEFT:
                            pos.x--;
                            break;
                        case MOVING_RIGHT:
                            pos.x++;
                            break;
                        case MOVING_UP:
                            pos.y--;
                            break;
                        case MOVING_DOWN:
                            pos.y++;
                            break;
                        default:
                            // IDLE
                            break;
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    threads.emplace_back([&running] {
        while (running) {
            { // Check if ComplexComponent has changed and update State
                auto lock = ecs.StartTransaction<Write<ComplexComponent, State>>();

                for (auto e : lock.EntitiesWith<ComplexComponent>()) {
                    if (e.Has<ComplexComponent, State>(lock)) {
                        // Only access components as read-only until we know we have work to do.
                        // This allows the transaction to skip Commit if no writes occured.
                        const ComplexComponent &readComp = e.GetPrevious<ComplexComponent>(lock);
                        if (readComp.HasChanged()) {
                            auto &state = e.Get<State>(lock);
                            state = (State)((state + 1) % STATE_COUNT);

                            ComplexComponent &writeComp = e.Get<ComplexComponent>(lock);
                            writeComp.ResetChanged();
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    running = false;
    for (auto &t : threads) {
        t.join();
    }

    auto trace = ecs.StopTrace();
    std::ofstream traceFile("example-trace.csv");
    trace.SaveToCSV(traceFile);

    return 0;
}
