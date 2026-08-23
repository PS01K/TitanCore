#pragma once

// =============================================================================
// TitanCore — State Manager
// =============================================================================
//
// The State Manager tracks the current state of all accounts on the
// TitanCore blockchain: how much ODM each address holds, and what their
// next expected transaction nonce is.
//
// ACCOUNT-BASED MODEL:
//   TitanCore uses an account-based model (like Ethereum), where each
//   address is like a bank account with two properties:
//     - balance: how much ODM they own
//     - nonce: how many transactions they've sent (prevents replays)
//
//   The alternative is the UTXO model (like Bitcoin), where ownership
//   is tracked through unspent transaction outputs. The account model
//   is simpler to understand and implement.
//
// STATE TRANSITIONS:
//   State changes when blocks are applied. Each transaction in a block
//   causes a state transition:
//
//     Before: Alice has 1000 ODM (nonce 0), Bob has 0 ODM (nonce 0)
//     Transaction: Alice → Bob: 50 ODM (nonce 0)
//     After:  Alice has 950 ODM (nonce 1), Bob has 50 ODM (nonce 0)
//
// ATOMICITY:
//   Block application is ALL-OR-NOTHING. If any transaction in the block
//   fails validation (insufficient balance, wrong nonce), the entire block
//   is rejected and NO state changes are applied. This prevents the
//   state from ending up in a half-updated, inconsistent condition.
//
//   Implementation: we copy the state, apply transactions to the copy,
//   and only swap in the new state if everything succeeds.
//
// GENESIS ALLOCATIONS:
//   Initial ODM enters the system through genesis allocations — a
//   hardcoded mapping of addresses to starting balances. This is how
//   ODM is "minted." For V1, each authority node gets 10,000 ODM.
//
// CUSTOM HASH FOR ADDRESS:
//   std::unordered_map needs a hash function for its key type.
//   The C++ standard library provides hash functions for primitive types
//   (int, string, etc.) but NOT for std::array. Since Address is
//   std::array<uint8_t, 20>, we need to define our own hash function.
//
//   Our AddressHash struct uses a simple but effective approach: since
//   addresses are already derived from cryptographic hashing (SHA-256
//   of the public key), they have excellent distribution. We can just
//   interpret the first 8 bytes as a size_t for the hash map bucket.
// =============================================================================

#include "titancore/common/types.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/transaction.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace titancore {
namespace core {

// =============================================================================
// Custom Hash for Address
// =============================================================================
//
// std::unordered_map<Address, ...> requires a hash function for Address.
//
// In C++, you provide a custom hash by creating a struct with an
// operator() that takes the key type and returns size_t.
//
// WHY THIS SIMPLE APPROACH WORKS:
//   Addresses are derived from SHA-256, which produces uniformly
//   distributed output. The first 8 bytes of a SHA-256 derived value
//   are already effectively random, so they make an excellent hash
//   for a hash table. No need for complex hash combining logic.
// =============================================================================

struct AddressHash {
    std::size_t operator()(const Address& addr) const {
        // Interpret the first sizeof(size_t) bytes of the address
        // as a size_t value. On a 64-bit system, this reads 8 bytes.
        //
        // We use memcpy instead of reinterpret_cast to avoid
        // undefined behavior from strict aliasing violations.
        // (Strict aliasing is a C++ rule that says you can't treat
        // memory of one type as another type — memcpy is the safe way.)
        std::size_t hash = 0;
        std::memcpy(&hash, addr.data(), sizeof(hash));
        return hash;
    }
};

// Convenience alias: a map keyed by Address, using our custom hash.
template <typename V>
using AddressMap = std::unordered_map<Address, V, AddressHash>;

// =============================================================================
// Account State
// =============================================================================

struct AccountState {
    uint64_t balance = 0;   // How much ODM this account holds
    uint64_t nonce   = 0;   // Next expected transaction nonce
};

// =============================================================================
// State Manager
// =============================================================================

class StateManager {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    // Initialize the state with genesis allocations.
    //
    // The map specifies initial balances: address → starting ODM.
    // Accounts are created with the specified balance and nonce 0.
    //
    // Example:
    //   StateManager state({
    //       {aliceAddr, 10000},
    //       {bobAddr, 10000},
    //   });
    explicit StateManager(const AddressMap<uint64_t>& genesisAllocations);

    // =========================================================================
    // Block Application
    // =========================================================================

    // Apply a block's transactions to the state.
    //
    // For each transaction in the block:
    //   1. Validate: sender exists, balance >= amount, nonce matches
    //   2. Debit sender: balance -= amount, nonce += 1
    //   3. Credit recipient: balance += amount (creates account if needed)
    //
    // ATOMICITY: If ANY transaction fails, the entire block is rejected
    // and state is unchanged. Uses copy-on-write internally.
    //
    // Returns true if all transactions were valid and state was updated.
    // Returns false if any transaction failed (state unchanged).
    bool applyBlock(const Block& block);

    // =========================================================================
    // Transaction Validation (read-only)
    // =========================================================================

    // Validate a single transaction against the CURRENT state.
    //
    // Checks:
    //   1. Sender account exists
    //   2. Sender has sufficient balance (balance >= amount)
    //   3. Transaction nonce matches sender's current nonce
    //
    // Does NOT modify state. Used by the mempool (future milestone)
    // to pre-validate transactions before they're included in a block.
    bool validateTransaction(const Transaction& tx) const;

    // Return a human-readable reason for the last validation failure.
    const std::string& getLastError() const;

    // =========================================================================
    // Query Methods
    // =========================================================================

    // Get the balance of an address. Returns 0 for unknown addresses.
    uint64_t getBalance(const Address& address) const;

    // Get the nonce of an address. Returns 0 for unknown addresses.
    uint64_t getNonce(const Address& address) const;

    // Check whether an account exists in the state.
    bool accountExists(const Address& address) const;

    // Get a read-only view of all accounts (for debugging/display).
    const AddressMap<AccountState>& getAllAccounts() const;

private:
    // The current state: a map from address to account info.
    AddressMap<AccountState> accounts_;

    // Human-readable error from the last failed operation.
    // mutable because validateTransaction() is const (it doesn't change
    // account state), but still needs to report errors. The 'mutable'
    // keyword tells C++: "this field can be modified even in const methods."
    // This is the standard pattern for caching/logging fields that don't
    // affect the logical state of the object.
    mutable std::string lastError_;
};

} // namespace core
} // namespace titancore
