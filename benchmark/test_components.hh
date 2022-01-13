#pragma once

#include "test_ecs.hh"

#include <Tecs.hh>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace testing {
    struct Transform {
        float mat4[16] = {
            // clang-format off
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
            // clang-format on
        };
    };

    struct Position {
        float pos[3] = {1, 0, 0};
    };

    struct Rotation {
        float rot[3] = {1, 0, 0};
    };

    struct Velocity {
        float vel[3] = {1, 0, 0};
    };

#define MAKE_STRUCT(x)                                                                                                 \
    struct x {                                                                                                         \
        float v = 1;                                                                                                   \
    }

    MAKE_STRUCT(A);
    MAKE_STRUCT(B);
    MAKE_STRUCT(C);
    MAKE_STRUCT(D);
    MAKE_STRUCT(E);
    MAKE_STRUCT(F);
    MAKE_STRUCT(G);
    MAKE_STRUCT(H);
    MAKE_STRUCT(I);
    MAKE_STRUCT(J);
    MAKE_STRUCT(K);
    MAKE_STRUCT(L);
    MAKE_STRUCT(M);
    MAKE_STRUCT(N);
    MAKE_STRUCT(O);
    MAKE_STRUCT(P);
    MAKE_STRUCT(Q);
    MAKE_STRUCT(R);
    MAKE_STRUCT(S);
    MAKE_STRUCT(T);
    MAKE_STRUCT(U);
    MAKE_STRUCT(V);
    MAKE_STRUCT(W);
    MAKE_STRUCT(X);
    MAKE_STRUCT(Y);
    MAKE_STRUCT(Z);
    MAKE_STRUCT(Data);

    template<typename T>
    struct MakeEntities {
        void operator()(Tecs::Lock<ECS2, Tecs::AddRemove> lock) const {
            for (size_t i = 0; i < 20; i++) {
                Tecs::Entity e = lock.NewEntity();
                e.Set<T>(lock);
                e.Set<Data>(lock);
            }
        }
    };
}; // namespace testing
