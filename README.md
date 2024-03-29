# Tecs (Thin, Transactional, Thread-safe ECS) [![Build status](https://badge.buildkite.com/48e772bb4b965d2edaa01fb82f13809680c2b060fc7cf3e7ae.svg?branch=master)](https://buildkite.com/frustra/tecs)

The goal of Tecs is to facilitate data storage and transfer between multiple threads,
with as little overhead as possible.

Tecs aims to be as thin a layer as possible between an application and its stored data,
while still providing thread-safe access. This is achieved by making heavy use of C++17 templates
to ensure memory access is safe at compile-time, and minimize the amount of logic remaining in the
compiled output.

Tecs is a core component of the [Stray Photons Game Engine](https://github.com/frustra/strayphotons)

### Features

 - Thread-safe transaction model for easy multi-threading
 - Compile-time safety checks ensure thread-safe operation
 - Context-switch free lock acquisition
 - Minimal read/write overhead for existing components
 - Efficient memory layout for maximum cache usage
 - Observer pattern for watching creations and deletions

### Theory of Operation

Tecs operates on read and write transactions, using 2 copies of the component data so that read and
write operations can be executed simultaneously. The only time a read transaction will block is when
a write transaction is being commited on the same component type.

Data is stored in such a way that it can be efficiently copied at a low level, minimizing the amount of
time read operations are blocked. To further minimize the overhead of locking and thread synchronization,
Tecs uses user-space locks (i.e. spinlocks) so no context-switching is required to check if a lock is free.

Storage architecture details can be found in [the docs](https://github.com/xthexder/Tecs/tree/master/docs).

### Entity Operations

A Tecs Entity on its own is just an id, but if a Lock is held by a Transaction, it can be used to
access Component data. The following Entity operations are only valid if the held Lock has the
correct permissions.

| Operation                                        | Required Permissions | Description                                                                 |
|--------------------------------------------------|----------------------|-----------------------------------------------------------------------------|
| `bool Entity::Exists`                            | `Read<Any>`          | Check if an Entity is valid and exists in the ECS instance.                 |
| `bool Entity::Has<T>`                            | `Read<Any>`          | Check if an Entity currently has a Component of type T.                     |
| `bool Entity::Had<T>`                            | `Read<Any>`          | Check if an Entity had a Component of T at the start of the Transaction.    |
| `const T &Entity::Get<T>`                        | `Read<T>`            | Read the current value of an Entity's T Component.                          |
| `T &Entity::Get<T>`                              | `Write<T>`           | Get a mutable reference to the current value of an Entity's T Component.    |
| `const T &Entity::Get<const T>`                  | `Read<T>`            | Get an immutable reference to the current value of an Entity's T Component. |
| `const T &Entity::GetPrevious<T>`                | `Read<T>`            | Read the value of an Entity's T Component at the start of the Transaction.  |
| `// Existing Component` <br> `T &Entity::Set<T>` | `Write<T>`           | Set the current value of an existing T Component. <br> Note: The existence of a component is checked at runtime and will throw an exception if the required permissions aren't held |
| `// New Component` <br> `T &Entity::Set<T>`      | `AddRemove`          | Add a new Component of type T to an Entity, or replace the current value.   |
| `void Entity::Unset<T>`                          | `AddRemove`          | Remove the T Component from an Entity.                                      |

### Event Operations

| Operation                    | Required Permissions | Description                     |
|------------------------------|----------------------|---------------------------------|
| `Observer<E> Lock::Watch<E>` | `AddRemove`          | Start watching for an event E.  |
| `bool Observer::Poll`        | `Read<Any>`          | Read the next observered event. |
| `void Observer::Stop`        | `AddRemove`          | Stop watching for an event.     |

### Event Types

 - `EntityEvent`
 - `ComponentEvent<ComponentType>`

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
        Tecs::Entity e = transaction.NewEntity();

        e.Set<Name>(transaction, std::to_string(i));
        e.Set<Position>(transaction);
    }
    // Add 100 entities with only Positions
    for (int i = 0; i < 100; i++) {
        transaction.NewEntity().Set<Position>(transaction);
    }

    // When `transaction` goes out of scope, it is deconstructed and
    // any changes made to entities will be commited to the ECS.
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
```
