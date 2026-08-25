#pragma once

// =============================================================================
// TitanCore — Mempool (Pending Transaction Pool)
// =============================================================================
//
// The Mempool is the waiting room for transactions. When a user creates
// a transaction, it goes into the mempool. When a validator produces a
// block, they pull transactions from the mempool. When the block is
// mined and added to the chain, those transactions are removed from
// the mempool.
//
// TRANSACTION LIFECYCLE:
//   1. User creates & signs a transaction
//   2. Transaction is submitted to the node's mempool
//   3. Mempool validates it (signature, balance, nonce)
//   4. Transaction sits in the mempool (status: PENDING)
//   5. Validator selects transactions from mempool for a new block
//   6. Block is created, signed, and added to the chain
//   7. StateManager applies the block (updates balances/nonces)
//   8. Mempool removes the mined transactions (status: CONFIRMED)
//   9. Mempool re-validates remaining transactions against new state
//
// PENDING STATE vs CONFIRMED STATE:
//   The mempool tracks TWO views of account state:
//
//   CONFIRMED STATE (from StateManager):
//     The balances and nonces that result from processing all mined blocks.
//     This is the "ground truth" — what's actually on the blockchain.
//
//   PENDING STATE (tracked by Mempool):
//     What balances and nonces WOULD be if all pending transactions were
//     also processed. This lets us accept multiple sequential transactions
//     from the same sender without waiting for each to be mined.
//
//   Example:
//     Confirmed: Alice has 10000 ODM, nonce 0
//     Pending tx1: Alice → Bob: 3000 (nonce 0)
//     Pending tx2: Alice → Carol: 2000 (nonce 1)
//     Pending state: Alice has 5000 ODM, nonce 2
//
//   Without pending state tracking, tx2 would be rejected because the
//   confirmed nonce is 0 but tx2 has nonce 1.
//
// PRE-VALIDATION:
//   The mempool rejects invalid transactions immediately:
//     1. Invalid signature → rejected (tampered or forged)
//     2. Duplicate → rejected (already in pool)
//     3. Unknown sender → rejected (no account, can't send)
//     4. Wrong nonce → rejected (must be sequential)
//     5. Insufficient balance → rejected (can't overspend)
//
//   This prevents spam, saves resources, and gives users immediate feedback.
//
// RE-VALIDATION:
//   After a new block is mined and state changes, some pending transactions
//   might become invalid (e.g., another node spent the sender's funds).
//   The revalidate() method re-checks all remaining transactions against
//   the new confirmed state and evicts any that are now invalid.
// =============================================================================

#include "titancore/common/types.hpp"
#include "titancore/core/state.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/core/block.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace titancore {
namespace core {

// =============================================================================
// Custom Hash for Hash type (std::array<uint8_t, 32>)
// =============================================================================
//
// Same technique as AddressHash in state.hpp, but for the 32-byte Hash type.
// We need this to use Hash as a key in std::unordered_map.
//
// Since Hash values come from SHA-256 (cryptographic, uniformly distributed),
// the first 8 bytes make an excellent hash table key.
// =============================================================================

struct HashHash {
    std::size_t operator()(const Hash& h) const {
        std::size_t hash = 0;
        std::memcpy(&hash, h.data(), sizeof(hash));
        return hash;
    }
};

// Convenience alias: a map keyed by Hash.
template <typename V>
using HashMap = std::unordered_map<Hash, V, HashHash>;

// =============================================================================
// Mempool Class
// =============================================================================

class Mempool {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    // Create a mempool that validates against the given state.
    //
    // The mempool holds a REFERENCE to the StateManager. This means:
    //   - The StateManager must outlive the Mempool
    //   - When the StateManager is updated (after applying a block),
    //     the mempool automatically sees the new state on next access
    //
    // This is intentional: the mempool needs to validate against the
    // current state, and holding a reference ensures it always sees
    // the latest confirmed balances and nonces.
    explicit Mempool(const StateManager& stateRef);

    // =========================================================================
    // Adding Transactions
    // =========================================================================

    // Attempt to add a transaction to the mempool.
    //
    // Performs five validation checks:
    //   1. SIGNATURE: verifyTransaction(tx) — cryptographic validity
    //   2. DUPLICATE: not already in the mempool
    //   3. SENDER EXISTS: sender has an account in confirmed state
    //   4. NONCE: tx.nonce must equal the sender's pending nonce
    //   5. BALANCE: pending balance must be >= transfer amount
    //
    // Returns true if accepted, false if rejected.
    // On rejection, call getLastError() for the reason.
    bool addTransaction(const Transaction& tx);

    // =========================================================================
    // Block Creation Support
    // =========================================================================

    // Get transactions ready for inclusion in a new block.
    //
    // Returns up to maxCount transactions, ordered so that each sender's
    // transactions appear in nonce order. This ensures the block's
    // transactions can be applied sequentially by the StateManager.
    //
    // Does NOT remove the transactions from the mempool — that happens
    // when the block is actually mined (removeMinedTransactions).
    std::vector<Transaction> getTransactionsForBlock(size_t maxCount) const;

    // =========================================================================
    // Post-Mining Cleanup
    // =========================================================================

    // Remove transactions that were included in a mined block.
    //
    // Called after a block has been added to the chain. Any transaction
    // in the block that's also in the mempool is removed.
    //
    // NOTE: This does NOT recalculate pending state. Call revalidate()
    // afterwards to rebuild pending nonces/balances and evict any
    // transactions that became invalid due to the state change.
    void removeMinedTransactions(const Block& block);

    // Re-validate all remaining transactions against the current state.
    //
    // This should be called after:
    //   1. removeMinedTransactions() (to clean up after a mined block)
    //   2. Any external state change
    //
    // The method:
    //   1. Saves all pending transactions
    //   2. Clears the mempool entirely (pending txs, nonces, balances)
    //   3. Sorts saved transactions by (sender, nonce)
    //   4. Tries to re-add each one against fresh state
    //   5. Any that fail are evicted
    //
    // Returns the number of transactions evicted.
    size_t revalidate();

    // =========================================================================
    // Queries
    // =========================================================================

    // Number of pending transactions in the mempool.
    size_t size() const;

    // Check if a transaction with the given hash is in the mempool.
    bool contains(const Hash& txHash) const;

    // Return the reason for the last failed addTransaction() call.
    const std::string& getLastError() const;

    // Get the pending nonce for an address.
    // Returns the state nonce if there are no pending transactions.
    uint64_t getPendingNonce(const Address& address) const;

    // Get the pending balance for an address.
    // Returns the state balance if there are no pending transactions.
    uint64_t getPendingBalance(const Address& address) const;

private:
    // Reference to the confirmed state (from mined blocks).
    const StateManager& state_;

    // Pending transactions, indexed by transaction hash for O(1) lookup.
    HashMap<Transaction> pending_;

    // Pending nonce per sender.
    // This is the NEXT expected nonce, accounting for all pending transactions.
    // If a sender has no pending transactions, their pending nonce equals
    // their confirmed nonce (from state_).
    AddressMap<uint64_t> pendingNonces_;

    // Pending balance per sender.
    // This is the available balance, accounting for all pending transactions.
    // If a sender has no pending transactions, their pending balance equals
    // their confirmed balance (from state_).
    AddressMap<uint64_t> pendingBalances_;

    // Human-readable error from the last failed addTransaction() call.
    std::string lastError_;
};

} // namespace core
} // namespace titancore
