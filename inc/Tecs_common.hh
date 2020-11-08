#pragma once

namespace Tecs {
    template<typename... Tn>
    class ECS;

    template<typename, typename...>
    class ReadLockRef {};
    template<typename, typename...>
    class ReadLock {};
    template<typename, typename...>
    class WriteLockRef {};
    template<typename, typename...>
    class WriteLock {};
    template<typename>
    class AddRemoveLockRef {};
    template<typename>
    class AddRemoveLock {};

    struct Entity;
} // namespace Tecs
