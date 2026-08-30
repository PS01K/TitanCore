#pragma once

// =============================================================================
// TitanCore — Blockchain (Chain Manager)
// =============================================================================
//
// The Blockchain class manages the complete, ordered sequence of blocks
// that forms the TitanCore ledger. It is the GATEKEEPER — every block
// must pass through it to become part of the canonical chain.
//
// WHAT THE BLOCKCHAIN CLASS DOES:
//   1. Initializes the chain with a genesis block
//   2. Validates and accepts new blocks (addBlock)
//   3. Validates the entire chain from genesis to tip
//   4. Provides query access to blocks and transactions
//
// WHAT IT ENFORCES:
//   - Sequential indices: Block N must be followed by N+1
//   - Chain linkage: Block N+1's previousHash must equal Block N's hash
//   - Block integrity: Every block must pass verifyBlock()
//   - Genesis consistency: The chain always starts with a valid genesis block
//   - PoA authority: When consensus is enabled, the correct authority must
//     produce each block according to the round-robin schedule
//
// STORAGE:
//   For V1, the chain is stored in a std::vector<Block>. This is:
//   - O(1) access by index (vector random access)
//   - O(N) lookup by hash (linear scan — fine for small chains)
//   - Simple and debuggable
//
//   In a future milestone, we'll add persistent storage (LevelDB/RocksDB)
//   behind the same public API, so callers won't need to change.
//
// ERROR HANDLING PHILOSOPHY:
//   addBlock() returns bool (true = accepted, false = rejected).
//   It does NOT throw exceptions for invalid blocks. In a real node,
//   you receive blocks from untrusted peers — invalid blocks are
//   expected, normal behavior, not exceptional errors.
//
//   Query methods like getBlock() DO throw on out-of-range access,
//   because that's a programming error (the caller should check height first).
// =============================================================================

#include "titancore/core/block.hpp"
#include "titancore/crypto/keys.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace titancore {
namespace core {

// Forward declaration to avoid circular include
class PoAConsensus;

class Blockchain {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    // Initialize a new blockchain with a genesis block.
    //
    // The genesis block is created and signed by the provided validator.
    // Every node in the network must use the same validator key pair for
    // genesis, so they all start with the same chain.
    //
    // After construction, the chain contains exactly one block (genesis)
    // and getHeight() returns 1.
    explicit Blockchain(const crypto::KeyPair& genesisValidator);

    // Initialize with PoA consensus enforcement.
    //
    // When constructed with a PoAConsensus pointer, addBlock() will
    // additionally check that each block's validator matches the
    // expected producer from the round-robin schedule.
    //
    // The PoAConsensus must outlive the Blockchain.
    Blockchain(const crypto::KeyPair& genesisValidator,
               PoAConsensus* consensus);

    // =========================================================================
    // Adding Blocks
    // =========================================================================

    // Attempt to add a block to the chain.
    //
    // Performs validation checks:
    //   1. INDEX: block.index must equal chain_.size() (next sequential index)
    //   2. LINKAGE: block.previousHash must equal the latest block's hash
    //   3. INTEGRITY: verifyBlock(block) must pass
    //   4. AUTHORITY: if PoA is enabled, the block's validator must match
    //      the expected producer for this index (round-robin schedule)
    //
    // Returns true if the block was accepted, false if rejected.
    // On rejection, the chain is unchanged.
    bool addBlock(const Block& block);

    // Return a human-readable reason why the last addBlock() call failed.
    // Empty string if the last addBlock() succeeded or none has been called.
    //
    // This is useful for logging and debugging:
    //   if (!chain.addBlock(block)) {
    //       spdlog::warn("Block rejected: {}", chain.getLastError());
    //   }
    const std::string& getLastError() const;

    // =========================================================================
    // Chain Validation
    // =========================================================================

    // Validate the ENTIRE chain from genesis to the tip.
    //
    // Checks every block:
    //   1. Block 0 has index 0 and all-zero previousHash
    //   2. Every block passes verifyBlock()
    //   3. Every block's index is sequential (0, 1, 2, ...)
    //   4. Every block's previousHash matches the previous block's hash
    //
    // Returns true if the entire chain is valid.
    //
    // WHEN TO USE THIS:
    //   - On node startup (to verify loaded chain from storage)
    //   - Periodically as a sanity check
    //   - After receiving a chain from another node
    //
    //   During normal operation, you don't need to call this — addBlock()
    //   validates each block as it's added. But this provides full-chain
    //   verification as a safety net.
    bool validateChain() const;

    // =========================================================================
    // Query Methods
    // =========================================================================

    // Get a block by its index (0-based).
    //
    // Throws std::out_of_range if index >= getHeight().
    // Use getHeight() to check bounds before calling.
    const Block& getBlock(uint64_t index) const;

    // Get the most recently added block (the chain tip).
    //
    // The chain always has at least one block (genesis), so this
    // never fails.
    const Block& getLatestBlock() const;

    // Get the chain height (number of blocks, including genesis).
    //
    // A freshly initialized chain has height 1 (just genesis).
    // After adding one block, height is 2, etc.
    uint64_t getHeight() const;

    // =========================================================================
    // Lookup Methods
    // =========================================================================

    // Find a block by its hash.
    //
    // Returns a pointer to the block if found, nullptr if not.
    // The pointer is valid as long as the Blockchain object exists
    // and no blocks are added (which could invalidate vector iterators).
    //
    // Performance: O(N) linear scan. For V1 with small chains this is fine.
    // A future optimization would be to maintain a hash→index map.
    const Block* findBlockByHash(const Hash& hash) const;

    // Find a transaction by its hash, searching all blocks.
    //
    // Returns a pointer to the transaction if found, nullptr if not.
    //
    // Performance: O(N*M) where N = blocks, M = avg transactions per block.
    // For V1 this is acceptable. A future optimization would be a
    // transaction hash → (block index, tx index) index.
    const Transaction* findTransaction(const Hash& txHash) const;

private:
    // The ordered sequence of blocks. Index 0 is always the genesis block.
    std::vector<Block> chain_;

    // Human-readable error message from the last failed addBlock() call.
    std::string lastError_;

    // Optional PoA consensus enforcer.
    // When non-null, addBlock() checks the authority schedule.
    // When null, any valid block is accepted (V0 behavior).
    PoAConsensus* consensus_ = nullptr;
};

} // namespace core
} // namespace titancore
