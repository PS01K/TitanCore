// =============================================================================
// TitanCore — Mempool Implementation
// =============================================================================
//
// This file implements the Mempool class — the pending transaction pool
// that validates, stores, and serves transactions for block creation.
//
// PENDING STATE TRACKING:
//   The key complexity in the mempool is tracking "pending" nonces and
//   balances. When Alice submits nonce 0, her pending nonce becomes 1.
//   When she submits nonce 1, her pending nonce becomes 2. And so on.
//   Similarly, each pending transaction reduces her pending balance.
//
//   This pending state is separate from the confirmed state in the
//   StateManager. It lets us accept multiple sequential transactions
//   from the same sender without waiting for each to be mined.
//
// RE-VALIDATION STRATEGY:
//   When state changes (after a block is mined), we take the "nuclear"
//   approach: save all pending transactions, clear everything, sort by
//   nonce, and try to re-add each one. This is O(N log N) where N is
//   the number of pending transactions, but it's simple and correct.
//
//   A more sophisticated approach would track which transactions are
//   affected by the state change and only re-validate those. But for
//   V1 with small mempools, the simple approach is better.
// =============================================================================

#include "titancore/core/mempool.hpp"
#include "titancore/crypto/hash.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace titancore {
namespace core {

// =============================================================================
// Construction
// =============================================================================

Mempool::Mempool(const StateManager& stateRef)
    : state_(stateRef) {
    spdlog::info("[Mempool] Initialized (empty)");
}

// =============================================================================
// Adding Transactions
// =============================================================================

bool Mempool::addTransaction(const Transaction& tx) {
    lastError_.clear();

    // --- Check 1: Cryptographic validity ---
    //
    // The transaction must have a valid ECDSA signature. This catches
    // tampered or forged transactions before they waste any more resources.
    if (!verifyTransaction(tx)) {
        lastError_ = "Invalid transaction signature";
        spdlog::warn("[Mempool] Tx rejected: {}", lastError_);
        return false;
    }

    // --- Check 2: Duplicate check ---
    //
    // If this exact transaction is already in the mempool, reject it.
    // This prevents the same transaction from being processed twice.
    if (pending_.count(tx.hash) > 0) {
        lastError_ = "Duplicate transaction: " + crypto::toHex(tx.hash);
        spdlog::warn("[Mempool] Tx rejected: {}", lastError_);
        return false;
    }

    // --- Check 3: Sender must exist in confirmed state ---
    //
    // The sender needs an account (from genesis allocations or from
    // having received ODM in a mined block). We don't allow sending
    // from accounts that only exist in pending state — that would be
    // too speculative.
    if (!state_.accountExists(tx.sender)) {
        lastError_ = "Sender account does not exist: " +
                      crypto::toHex(tx.sender);
        spdlog::warn("[Mempool] Tx rejected: {}", lastError_);
        return false;
    }

    // --- Determine pending nonce and balance ---
    //
    // If the sender already has pending transactions, use the pending
    // nonce/balance. Otherwise, use the confirmed state values.
    uint64_t expectedNonce;
    auto nonceIt = pendingNonces_.find(tx.sender);
    if (nonceIt != pendingNonces_.end()) {
        expectedNonce = nonceIt->second;
    } else {
        expectedNonce = state_.getNonce(tx.sender);
    }

    uint64_t availableBalance;
    auto balanceIt = pendingBalances_.find(tx.sender);
    if (balanceIt != pendingBalances_.end()) {
        availableBalance = balanceIt->second;
    } else {
        availableBalance = state_.getBalance(tx.sender);
    }

    // --- Check 4: Nonce must be sequential ---
    //
    // The transaction's nonce must exactly match the expected pending nonce.
    // This ensures transactions are processed in order:
    //   - Nonce too low: replay of an already-pending or already-mined tx
    //   - Nonce too high: gap in the sequence (tx would be unprocessable)
    if (tx.nonce != expectedNonce) {
        lastError_ = "Wrong nonce: expected " +
                      std::to_string(expectedNonce) + ", got " +
                      std::to_string(tx.nonce);
        spdlog::warn("[Mempool] Tx rejected: {}", lastError_);
        return false;
    }

    // --- Check 5: Sufficient pending balance ---
    //
    // The sender must have enough ODM remaining after all their other
    // pending transactions are accounted for.
    if (availableBalance < tx.amount) {
        lastError_ = "Insufficient pending balance: has " +
                      std::to_string(availableBalance) +
                      " ODM available, needs " +
                      std::to_string(tx.amount) + " ODM";
        spdlog::warn("[Mempool] Tx rejected: {}", lastError_);
        return false;
    }

    // --- All checks passed — accept the transaction ---

    // Update pending state for this sender
    pendingNonces_[tx.sender] = tx.nonce + 1;
    pendingBalances_[tx.sender] = availableBalance - tx.amount;

    // Store the transaction
    pending_[tx.hash] = tx;

    spdlog::info("[Mempool] Tx accepted: {} ({} ODM, nonce {})",
                 crypto::toHex(tx.hash), tx.amount, tx.nonce);

    return true;
}

// =============================================================================
// Block Creation Support
// =============================================================================

std::vector<Transaction> Mempool::getTransactionsForBlock(size_t maxCount) const {
    // Collect all pending transactions into a vector
    std::vector<Transaction> transactions;
    transactions.reserve(pending_.size());

    for (const auto& [hash, tx] : pending_) {
        transactions.push_back(tx);
    }

    // Sort by sender address first, then by nonce within each sender.
    //
    // This ensures that each sender's transactions appear in nonce order,
    // which is required for the StateManager to process them correctly.
    //
    // Example output:
    //   Alice nonce 0, Alice nonce 1, Alice nonce 2, Bob nonce 0, Bob nonce 1
    std::sort(transactions.begin(), transactions.end(),
        [](const Transaction& a, const Transaction& b) {
            if (a.sender != b.sender) {
                return a.sender < b.sender;
            }
            return a.nonce < b.nonce;
        }
    );

    // Limit to maxCount
    if (transactions.size() > maxCount) {
        transactions.resize(maxCount);
    }

    return transactions;
}

// =============================================================================
// Post-Mining Cleanup
// =============================================================================

void Mempool::removeMinedTransactions(const Block& block) {
    size_t removed = 0;

    for (const auto& tx : block.transactions) {
        auto it = pending_.find(tx.hash);
        if (it != pending_.end()) {
            pending_.erase(it);
            ++removed;
        }
    }

    if (removed > 0) {
        spdlog::info("[Mempool] Removed {} mined transactions", removed);
    }
}

size_t Mempool::revalidate() {
    // --- Step 1: Save all pending transactions ---
    std::vector<Transaction> savedTxs;
    savedTxs.reserve(pending_.size());
    for (const auto& [hash, tx] : pending_) {
        savedTxs.push_back(tx);
    }

    size_t beforeCount = savedTxs.size();

    // --- Step 2: Clear everything ---
    //
    // We wipe the entire mempool state and rebuild from scratch.
    // This is the simplest correct approach — no edge cases around
    // partially updated pending nonces/balances.
    pending_.clear();
    pendingNonces_.clear();
    pendingBalances_.clear();
    lastError_.clear();

    // --- Step 3: Sort by sender, then nonce ---
    //
    // We must re-add transactions in nonce order per sender,
    // because addTransaction checks sequential nonces.
    std::sort(savedTxs.begin(), savedTxs.end(),
        [](const Transaction& a, const Transaction& b) {
            if (a.sender != b.sender) {
                return a.sender < b.sender;
            }
            return a.nonce < b.nonce;
        }
    );

    // --- Step 4: Try to re-add each transaction ---
    //
    // Some will succeed (still valid against new state), others will
    // fail (e.g., their sender's balance was reduced by another block).
    //
    // IMPORTANT: If a transaction with nonce N fails, ALL subsequent
    // transactions from the same sender (nonce N+1, N+2, ...) will also
    // fail because they depend on N being processed first. This is
    // correct behavior — those transactions are now unprocessable.
    for (const auto& tx : savedTxs) {
        addTransaction(tx);
        // Silently ignore failures — evicted transactions are just gone
    }

    size_t afterCount = pending_.size();
    size_t evicted = beforeCount - afterCount;

    if (evicted > 0) {
        spdlog::info("[Mempool] Revalidation: {} evicted, {} remaining",
                     evicted, afterCount);
    }

    return evicted;
}

// =============================================================================
// Queries
// =============================================================================

size_t Mempool::size() const {
    return pending_.size();
}

bool Mempool::contains(const Hash& txHash) const {
    return pending_.count(txHash) > 0;
}

const std::string& Mempool::getLastError() const {
    return lastError_;
}

uint64_t Mempool::getPendingNonce(const Address& address) const {
    auto it = pendingNonces_.find(address);
    if (it != pendingNonces_.end()) {
        return it->second;
    }
    return state_.getNonce(address);
}

uint64_t Mempool::getPendingBalance(const Address& address) const {
    auto it = pendingBalances_.find(address);
    if (it != pendingBalances_.end()) {
        return it->second;
    }
    return state_.getBalance(address);
}

} // namespace core
} // namespace titancore
