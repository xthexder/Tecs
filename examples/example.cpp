#include "complex_component.hh"
#include "components.hh"

#include <iostream>
#include <vector>

using namespace example;
using namespace Tecs;

// Instantiate an ECS using our defined World
static World ecs;

int main(int argc, char **argv) {
    { // Start a new transaction with AddRemove permissions to create new entities and components
        auto transaction = ecs.StartTransaction<AddRemove>();

        // Add 10 entities with Names and Positions
        for (int i = 0; i < 10; i++) {
            Tecs::Entity e = transaction.NewEntity();

            e.Set<Name>(transaction, std::to_string(i));
            e.Set<Position>(transaction);
        }
        // Add 100 entities with only Positions
        for (int i = 0; i < 100; i++) {
            transaction.NewEntity().Set<Position>(transaction);
        }

        // When `transaction` goes out of scope, it is deconstructed and any changes made to entities will be commited
        // to the ECS.
    }

    { // Start a read transaction to access entity data
        auto transaction = ecs.StartTransaction<Read<Name, Position>>();

        // List entities with a certain type of component
        const std::vector<Entity> &entities = transaction.EntitiesWith<Name>();

        // Loop through entities with both a Name and Position component
        for (auto e : entities) {
            if (!e.Has<Name, Position>(transaction)) continue;

            // Read the entity's Name and Position
            const std::string &name = e.Get<Name>(transaction);
            const Position &pos = e.Get<Position>(transaction);

            std::cout << "Entity: " << name << " at (" << pos.x << ", " << pos.y << ")" << std::endl;
        }
    }

    { // Start a write transaction to modify entity data
        auto transaction = ecs.StartTransaction<Read<Name>, Write<Position>>();

        // Loop through entities with a Position
        for (auto e : transaction.EntitiesWith<Position>()) {
            // Move the entity to the right
            Position &pos = e.Get<Position>(transaction);
            pos.x++;

            // Only print the entity if it has a name
            if (e.Has<Name>(transaction)) {
                const std::string &name = e.Get<Name>(transaction);
                std::cout << "Moving " << name << " to (" << pos.x << ", " << pos.y << ")" << std::endl;
            }
        }
    }

    return 0;
}
