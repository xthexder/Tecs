#include "gen_ecs.hh"
#include "gen_entity.hh"
#include "gen_lock.hh"

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "Usage: codegen out/lock.h out/entity.h out/ecs.cc out/entity.cc out/lock.cc";
        return -1;
    }
    {
        auto out = std::ofstream(argv[1], std::ios::trunc);
        generateLockH(out);
    }
    {
        auto out = std::ofstream(argv[2], std::ios::trunc);
        generateEntityH(out);
    }
    {
        auto out = std::ofstream(argv[3], std::ios::trunc);
        generateECSCC(out);
    }
    {
        auto out = std::ofstream(argv[4], std::ios::trunc);
        generateEntityCC(out);
    }
    {
        auto out = std::ofstream(argv[5], std::ios::trunc);
        generateLockCC(out);
    }
}
