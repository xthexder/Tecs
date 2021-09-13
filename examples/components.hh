#include <Tecs.hh>
#include <string>

namespace example {
    // Define 4 Component types:
    struct Position {
        int x, y;

        Position() : x(0), y(0) {}
        Position(int x, int y) : x(x), y(y) {}
    };
    enum State { IDLE = 0, MOVING_LEFT, MOVING_RIGHT, MOVING_UP, MOVING_DOWN, STATE_COUNT };
    typedef std::string Name;
    // These can also be forward-declarations of Components defined elsewhere
    class ComplexComponent;

    // Define a World with each Component type predefined
    using World = Tecs::ECS<Position, State, Name, ComplexComponent>;

    // Define the stream operator to make printing State easier
    static inline std::ostream &operator<<(std::ostream &out, const State &t) {
        static const std::array stateNames = {
            "IDLE",
            "MOVING_LEFT",
            "MOVING_RIGHT",
            "MOVING_UP",
            "MOVING_DOWN",
        };
        return out << stateNames[(size_t)t];
    }
} // namespace example

// Name the components so they show up nicely in performance traces
TECS_NAME_COMPONENT(example::Position, "Position");
TECS_NAME_COMPONENT(example::State, "State");
TECS_NAME_COMPONENT(example::Name, "Name");
TECS_NAME_COMPONENT(example::ComplexComponent, "ComplexComponent");
