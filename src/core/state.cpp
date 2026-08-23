// =============================================================================
// TitanCore — State Manager Implementation
// =============================================================================
//
// This file implements the StateManager class, which tracks account
// balances and nonces and processes blocks to update state.
//
// THE COPY-ON-WRITE PATTERN:
//   When applyBlock() is called, we don't modify the live state directly.
//   Instead:
//     1. Copy the current accounts_ map into a local variable
//     2. Apply all transactions to the COPY
//     3. If everything succeeds, swap the copy into accounts_
//     4. If anything fails, discard the copy (accounts_ is unchanged)
//
//   This guarantees atomicity: the state is either fully updated or
//   completely unchanged. There's no risk of a "half-applied" block
//   leaving the state in an inconsistent condition.
//
//   The trade-off is memory: we temporarily have two copies of the
//   state in memory. For V1 with small chains and few accounts, this
//   is perfectly fine. Production blockchains use more sophisticated
//   approaches (journaling, snapshots, etc.).
//
// ACCOUNT CREATION:
//   Accounts are created in two ways:
//     1. Genesis allocations (constructor) — initial ODM distribution
//     2. Receiving a transfer — if someone sends ODM to an address
//        that doesn't exist yet, the account is created automatically
//        with balance = transfer amount and nonce = 0.
//
//   This means you don't need to "register" an account before using it.
//   Just generate a key pair, derive an address, and you're ready to
//   receive ODM. The account springs into existence on first credit.
// =============================================================================

#include "titancore/core/state.hpp"
#include "titancore/crypto/hash.hpp"

#include <spdlog/spdlog.h>

namespace titancore {
namespace core {

// =============================================================================
// Construction
// =============================================================================

StateManager::StateManager(const AddressMap<uint64_t>& genesisAllocations) {
    // Create an account for each genesis allocation.
    // Each account starts with the specified balance and nonce 0.
    for (const auto& [address, balance] : genesisAllocations) {
        accounts_[address] = AccountState{balance, 0};
        spdlog::info("[State] Genesis allocation: {} → {} ODM",
                     crypto::toHex(address), balance);
    }

    spdlog::info("[State] Initialized with {} accounts", accounts_.size());
}

// =============================================================================
// Transaction Validation (read-only)
// =============================================================================

bool StateManager::validateTransaction(const Transaction& tx) const {
    lastError_.clear();

    // --- Check 1: Does the sender account exist? ---
    //
    // If the sender has never received ODM and wasn't in the genesis
    // allocations, they can't send anything.
    auto it = accounts_.find(tx.sender);
    if (it == accounts_.end()) {
        lastError_ = "Sender account does not exist: " +
                      crypto::toHex(tx.sender);
        return false;
    }

    const AccountState& account = it->second;

    // --- Check 2: Does the sender have enough ODM? ---
    //
    // This is the core double-spend prevention. The sender's balance
    // must be >= the transfer amount.
    if (account.balance < tx.amount) {
        lastError_ = "Insufficient balance: has " +
                      std::to_string(account.balance) + " ODM, needs " +
                      std::to_string(tx.amount) + " ODM";
        return false;
    }

    // --- Check 3: Does the nonce match? ---
    //
    // The transaction's nonce must EXACTLY equal the account's current
    // nonce. This prevents:
    //   - Replay attacks: resubmitting an old transaction (its nonce
    //     would be less than the current nonce)
    //   - Transaction gaps: submitting nonce 5 when the account is
    //     at nonce 3 (nonces must be sequential)
    if (tx.nonce != account.nonce) {
        lastError_ = "Wrong nonce: expected " +
                      std::to_string(account.nonce) + ", got " +
                      std::to_string(tx.nonce);
        return false;
    }

    return true;
}

// =============================================================================
// Block Application
// =============================================================================

bool StateManager::applyBlock(const Block& block) {
    lastError_.clear();

    // --- Step 1: Copy the current state ---
    //
    // This is the "copy" part of copy-on-write. We apply all
    // transactions to this copy. If anything fails, we just
    // discard it and accounts_ is unchanged.
    AddressMap<AccountState> newState = accounts_;

    // --- Step 2: Apply each transaction to the copy ---
    for (size_t i = 0; i < block.transactions.size(); ++i) {
        const Transaction& tx = block.transactions[i];

        // Validate against the evolving new state (not the original).
        // This is important: if Alice sends two transactions in the
        // same block, the second one must see the balance/nonce
        // changes from the first one.

        // Check sender exists
        auto senderIt = newState.find(tx.sender);
        if (senderIt == newState.end()) {
            lastError_ = "Tx " + std::to_string(i) +
                          ": sender does not exist: " +
                          crypto::toHex(tx.sender);
            spdlog::warn("[State] Block rejected: {}", lastError_);
            return false;  // Discard newState, accounts_ unchanged
        }

        AccountState& senderAccount = senderIt->second;

        // Check sufficient balance
        if (senderAccount.balance < tx.amount) {
            lastError_ = "Tx " + std::to_string(i) +
                          ": insufficient balance: has " +
                          std::to_string(senderAccount.balance) +
                          " ODM, needs " + std::to_string(tx.amount) + " ODM";
            spdlog::warn("[State] Block rejected: {}", lastError_);
            return false;
        }

        // Check nonce
        if (tx.nonce != senderAccount.nonce) {
            lastError_ = "Tx " + std::to_string(i) +
                          ": wrong nonce: expected " +
                          std::to_string(senderAccount.nonce) +
                          ", got " + std::to_string(tx.nonce);
            spdlog::warn("[State] Block rejected: {}", lastError_);
            return false;
        }

        // --- All checks passed — apply the transfer ---

        // Debit sender
        senderAccount.balance -= tx.amount;
        senderAccount.nonce += 1;

        // Credit recipient (create account if it doesn't exist)
        //
        // The [] operator on unordered_map creates a default-constructed
        // entry if the key doesn't exist. AccountState has default
        // values {balance: 0, nonce: 0}, so new accounts start empty.
        newState[tx.recipient].balance += tx.amount;
    }

    // --- Step 3: All transactions succeeded — commit the new state ---
    //
    // std::swap is O(1) — it just swaps pointers to the internal
    // hash table data, not copying all the elements.
    accounts_ = std::move(newState);

    spdlog::info("[State] Block {} applied: {} transactions processed",
                 block.index, block.transactions.size());

    return true;
}

// =============================================================================
// Error Reporting
// =============================================================================

const std::string& StateManager::getLastError() const {
    return lastError_;
}

// =============================================================================
// Query Methods
// =============================================================================

uint64_t StateManager::getBalance(const Address& address) const {
    auto it = accounts_.find(address);
    if (it == accounts_.end()) {
        return 0;  // Unknown address has zero balance
    }
    return it->second.balance;
}

uint64_t StateManager::getNonce(const Address& address) const {
    auto it = accounts_.find(address);
    if (it == accounts_.end()) {
        return 0;  // Unknown address has nonce 0
    }
    return it->second.nonce;
}

bool StateManager::accountExists(const Address& address) const {
    return accounts_.find(address) != accounts_.end();
}

const AddressMap<AccountState>& StateManager::getAllAccounts() const {
    return accounts_;
}

} // namespace core
} // namespace titancore
