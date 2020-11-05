# Tecs (Transactional, Thread-safe, Templated ECS)

The goal of Tecs is to facilitate data storage and transfer between multiple threads, with as little overhead as possible.

Tecs aims to be as thin a layer as possible between an application and its stored data, while still providing thread-safe access.
This is achieved by making heavy use of C++17 Templates to calculate memory addresses at compile-time, and inlining all
operations to maximize compiler optimization.

Tecs operates on read and write transactions, using 2 copies of the data so that read and write operations can be executed
simultaneously. The only time a read transaction will block is when a write transaction is being commited on the same Component type.
Data is stored in such a way that it can be efficiently copied at a low level, minimizing the time read operations are blocked.
To further minimize the overhead of locking and thread synchronization, iTecs uses user-space locks (i.e. spinlocks) so no
context switching is required to check if a lock is free.

Storage architecture details can be found in [the docs](https://github.com/xthexder/Tecs/tree/master/docs).
