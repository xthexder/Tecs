#include <Tecs.hh>

namespace Tecs {
    // Used for detecting nested transactions
    thread_local std::vector<size_t> activeTransactions;
    std::atomic_size_t nextEcsId(0);
} // namespace Tecs
