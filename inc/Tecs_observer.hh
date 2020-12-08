#pragma once

#include "Tecs_entity.hh"
#include "Tecs_locks.hh"

#include <deque>
#include <memory>
#include <thread>
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

    /**
     * An Observer is a handle to an event queue. The queue can be consumed from within any transaction. Observer
     * handles should be local to a thread, and not shared.
     *
     * An Observer will persist until the ECS instance is decontructed, unless Observer::Stop() is called from within an
     * AddRemove Transaction.
     */
    template<typename ECSType, typename EventType>
    class Observer {
    public:
        Observer() : ecs(nullptr) {}
        Observer(ECSType &ecs, std::shared_ptr<std::deque<EventType>> &eventList)
            : ecs(&ecs), eventListWeak(eventList) {}

        /**
         * Poll for the next event that occured. Returns false if there are no more events.
         * Events will be returned in the order they occured, up until the start of the current transaction.
         */
        bool Poll(Lock<ECSType> lock, EventType &eventOut) const {
            auto eventList = eventListWeak.lock();
            if (eventList && !eventList->empty()) {
                eventOut = eventList->front();
                eventList->pop_front();
                return true;
            }
            return false;
        }

        void Stop(Lock<ECSType, AddRemove> lock) {
            lock.StopWatching(*this);
        }

        friend bool operator==(
            const std::shared_ptr<std::deque<EventType>> &lhs, const Observer<ECSType, EventType> &rhs) {
            return lhs == rhs.eventListWeak.lock();
        }

    private:
        ECSType *ecs;
        std::weak_ptr<std::deque<EventType>> eventListWeak;

        template<typename, typename...>
        friend class Lock;
    };
}; // namespace Tecs
