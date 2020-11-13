# Tecs (Transactional, Thread-safe, Templated ECS)

The goal of Tecs is to facilitate data storage and transfer between multiple threads, with as little overhead as possible.

Tecs aims to be as thin a layer as possible between an application and its stored data, while still providing thread-safe access.
This is achieved by making heavy use of C++17 Templates to calculate memory addresses at compile-time, and inlining all
operations to maximize compiler optimization.

### Theory of Operation

Tecs operates on read and write transactions, using 2 copies of the data so that read and write operations can be executed
simultaneously. The only time a read transaction will block is when a write transaction is being commited on the same Component type.
Data is stored in such a way that it can be efficiently copied at a low level, minimizing the time read operations are blocked.
To further minimize the overhead of locking and thread synchronization, Tecs uses user-space locks (i.e. spinlocks) so no
context switching is required to check if a lock is free.

Storage architecture details can be found in [the docs](https://github.com/xthexder/Tecs/tree/master/docs).

## Examples

example.hh
```c++
#include <Tecs.hh>
#include <string>

// Define 3 Component types:
struct Position {
    int x, y;
};
typedef std::string Name;
// These can also be forward-declarations of Components defined elsewhere.
class ComplexComponent;

// Define a World with each Component type predefined
using World = Tecs::ECS<Position, Name, ComplexComponent>;
```

example.cpp
```c++
// Instantiate an ECS using our defined World
static World ecs;

{ // Start a new transaction with AddRemove permissions to create new entities and components
    auto transaction = ecs.StartTransaction<AddRemove>();

    // Add 10 entities with Names and Positions
    for (int i = 0; i < 10; i++) {
        Tecs::Entity e = transaction.AddEntity();

        e.Set<Name>(transaction, std::to_string(i));
        e.Set<Position>(transaction);
    }
    // Add 100 entities with only Positions
    for (int i = 0; i < 100; i++) {
        transaction.AddEntity().Set<Position>(transaction);
    }

    // When `transaction` goes out of scope, it is deconstructed and any changes made to entities will be commited
    // to the ECS.
}

{ // Start a read transaction to access entity data
    auto transaction = ecs.StartTransaction<Read<Name, Position>>();

    // List entities with a certain type of component
    const std::vector<Entity> &entities = transaction.ValidEntities<Name>();

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
    for (auto e : transaction.ValidEntities<Position>()) {
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
```
