#include <Tecs.hh>

namespace Tecs {
    // Used for detecting nested transactions
    thread_local std::array<size_t, TECS_MAX_ACTIVE_TRANSACTIONS_PER_THREAD> activeTransactions;
    thread_local size_t activeTransactionsCount = 0;
    std::atomic_size_t nextEcsId(0);
    std::atomic_size_t nextTransactionId(0);
} // namespace Tecs
