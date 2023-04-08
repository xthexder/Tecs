#pragma once

#include "Tecs_entity.hh"
#include "Tecs_permissions.hh"

#include <deque>
#include <memory>
#include <thread>
#include <tuple>
#include <type_traits>

namespace Tecs {
    enum class EventType {
        INVALID = 0,
        ADDED,
        REMOVED,
    };

    template<typename T>
    struct ComponentEvent {
        EventType type;
        Entity entity;
        T component;

        ComponentEvent() : type(EventType::INVALID), entity(), component() {}
        ComponentEvent(EventType type, const Entity &entity, const T &component)
            : type(type), entity(entity), component(component) {}
    };

    struct EntityEvent {
        EventType type;
        Entity entity;

        EntityEvent() : type(EventType::INVALID), entity() {}
        EntityEvent(EventType type, const Entity &entity) : type(type), entity(entity) {}
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

        operator bool() const {
            return ecs != nullptr && !eventListWeak.expired();
        }

        friend bool operator==(const std::shared_ptr<std::deque<EventType>> &lhs,
            const Observer<ECSType, EventType> &rhs) {
            return lhs == rhs.eventListWeak.lock();
        }

    private:
        ECSType *ecs;
        std::weak_ptr<std::deque<EventType>> eventListWeak;

        template<typename, typename>
        friend class LockImpl;
    };
}; // namespace Tecs
