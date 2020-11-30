#pragma once

#include "Tecs_entity.hh"
#include "Tecs_locks.hh"

#include <deque>
#include <tuple>

namespace Tecs {
    template<typename T>
    struct Added {
        Entity entity;
        T component;

        Added() : entity(), component() {}
        Added(const Entity &entity, const T &component) : entity(entity), component(component) {}
    };

    template<typename T>
    struct Removed {
        Entity entity;
        T component;

        Removed() : entity(), component() {}
        Removed(const Entity &entity, const T &component) : entity(entity), component(component) {}
    };

    struct EntityAdded {
        Entity entity;

        EntityAdded() : entity() {}
        EntityAdded(const Entity &entity) : entity(entity) {}
    };

    struct EntityRemoved {
        Entity entity;

        EntityRemoved() : entity() {}
        EntityRemoved(const Entity &entity) : entity(entity) {}
    };

    template<typename EventType>
    class Observer {
    public:
        Observer(std::deque<EventType> *eventList = nullptr) : eventList(eventList) {}

        template<typename ECSType>
        bool Poll(Lock<ECSType> lock, EventType &eventOut) {
            if (eventList != nullptr && !eventList->empty()) {
                eventOut = eventList->front();
                eventList->pop_front();
                return true;
            }
            return false;
        }

    private:
        std::deque<EventType> *eventList;
    };
}; // namespace Tecs
