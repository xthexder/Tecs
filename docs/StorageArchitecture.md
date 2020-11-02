+-----------------------------------------------------------+
|                                                           |
|                  ECS Instance Storage                     |
|                                                           |
| +------------------------------+ +----------------------+ |
| |                              | |      Tuple of:       | |
| |    ComponentIndex<Bitset>    | +----------------------+ |
| |                              | |  ComponentIndex<T1>  | |
| | Stores a bitset representing | +----------------------+ |
| | which components are valid   | |  ComponentIndex<T2>  | |
| | for each entity.             | +----------------------+ |
| |                              | |  ComponentIndex<Tn>  | |
| +------------------------------+ +----------------------+ |
+-----------------------------------------------------------+

+-----------------------------------------------------------+
|                                                           |
|                     ComponentIndex<T>                     |
|                                                           |
| +-------------------------------------------------------+ |
| |                                                       | |
| |        Read-Write (i.e. Shared) Spinlock Mutex        | |
| |                                                       | |
| | Allows multiple readers and a single writer to access | |
| | their respective datasets concurrently. During writer | |
| | unlock (i.e. write commit) both datasets are locked   | |
| | exclusively while all changes to the write dataset    | |
| | are applied to the read dataset.                      | |
| |                                                       | |
| +-------------------------------------------------------+ |
| +---------------------------+---------------------------+ |
| |         Read Set:         |         Write Set:        | |
| +---------------------------+---------------------------+ |
| |                           |                           | |
| | - Vector of T             | - Vector of T             | |
| |                           |                           | |
| | - Vector of valid indexes | - Vector of valid indexes | |
| |                           |                           | |
| |                           | - Valid-list changed flag | |
| |                           |                           | |
| +---------------------------+---------------------------+ |
+-----------------------------------------------------------+
