#pragma once

#include "utils.hh"

#include <Tecs.hh>

namespace testing {
    struct Transform;
    struct Position;
    struct Rotation;
    struct Velocity;

    using ECS = Tecs::ECS<Transform, Position, Rotation, Velocity>;

    struct A;
    struct B;
    struct C;
    struct D;
    struct E;
    struct F;
    struct G;
    struct H;
    struct I;
    struct J;
    struct K;
    struct L;
    struct M;
    struct N;
    struct O;
    struct P;
    struct Q;
    struct R;
    struct S;
    struct T;
    struct U;
    struct V;
    struct W;
    struct X;
    struct Y;
    struct Z;
    struct Data;
    using ECS2 = Tecs::ECS<A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, Data>;
    using ECS3 = Tecs::ECS<A, B, C, D, E>;
}; // namespace testing
