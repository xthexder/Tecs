#include "gen_ecs.hh"
#include "gen_entity.hh"
#include "gen_lock.hh"

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "Usage: codegen out/ecs.hh out/entity.hh out/lock.hh";
        return -1;
    }
    {
        auto out = std::ofstream(argv[1], std::ios::trunc);
        generateECSCC(out);
    }
    {
        auto out = std::ofstream(argv[2], std::ios::trunc);
        generateEntityCC(out);
    }
    {
        auto out = std::ofstream(argv[3], std::ios::trunc);
        generateLockCC(out);
    }
}
