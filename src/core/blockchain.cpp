// =============================================================================
// TitanCore — Blockchain Implementation
// =============================================================================
//
// This file implements the Blockchain class — the chain manager that
// holds the ordered sequence of blocks and enforces all chain-level rules.
//
// THE GATEKEEPER PATTERN:
//   The Blockchain class is the single point of entry for blocks.
//   No block becomes part of the canonical chain without passing through
//   addBlock(), which performs full validation. This pattern ensures
//   that the chain is ALWAYS in a valid state — you can never get a
//   chain with a gap in indices, a broken previousHash link, or an
//   invalid block.
//
// VALIDATION LAYERS:
//   When addBlock() is called, validation happens in two layers:
//
//   Layer 1: Chain-level rules (checked by Blockchain)
//     - Is the index sequential?
//     - Does previousHash match the latest block?
//
//   Layer 2: Block-level rules (checked by verifyBlock)
//     - Does the hash match the header?
//     - Is the signature valid?
//     - Does the validator's key match their address?
//     - Are all transactions valid?
//
//   This separation means verifyBlock() can be used independently
//   (e.g., to validate a block before forwarding it to another node),
//   while addBlock() adds the chain-context checks on top.
// =============================================================================

#include "titancore/core/blockchain.hpp"
#include "titancore/core/consensus.hpp"
#include "titancore/crypto/hash.hpp"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace titancore {
namespace core {

// =============================================================================
// Construction
// =============================================================================

Blockchain::Blockchain(const crypto::KeyPair& genesisValidator) {
    Block genesis = createGenesisBlock(genesisValidator);
    chain_.push_back(std::move(genesis));

    spdlog::info("[Blockchain] Initialized with genesis block: {}",
                 crypto::toHex(chain_[0].hash));
}

Blockchain::Blockchain(const crypto::KeyPair& genesisValidator,
                       PoAConsensus* consensus)
    : consensus_(consensus) {
    Block genesis = createGenesisBlock(genesisValidator);
    chain_.push_back(std::move(genesis));

    spdlog::info("[Blockchain] Initialized with genesis block: {} (PoA enabled)",
                 crypto::toHex(chain_[0].hash));
}

// =============================================================================
// Adding Blocks
// =============================================================================

bool Blockchain::addBlock(const Block& block) {
    // Clear any previous error message
    lastError_.clear();

    // --- Check 1: Sequential index ---
    //
    // The new block's index must be exactly chain_.size().
    // If the chain has blocks [0, 1, 2], the next block must have index 3.
    //
    // This prevents:
    //   - Gaps (e.g., adding block 5 when chain height is 3)
    //   - Duplicates (e.g., adding another block 2)
    //   - Out-of-order blocks (e.g., adding block 1 when chain height is 3)
    uint64_t expectedIndex = chain_.size();
    if (block.index != expectedIndex) {
        lastError_ = "Wrong index: expected " + std::to_string(expectedIndex) +
                      ", got " + std::to_string(block.index);
        spdlog::warn("[Blockchain] Block rejected: {}", lastError_);
        return false;
    }

    // --- Check 2: Chain linkage ---
    //
    // The new block's previousHash must match the hash of the current
    // latest block. This is THE chain link — it's what makes tampering
    // with historical blocks detectable.
    //
    // Without this check, someone could create a valid block (correct
    // signature, valid transactions) but attach it to a different chain.
    const Hash& latestHash = getLatestBlock().hash;
    if (block.previousHash != latestHash) {
        lastError_ = "Previous hash mismatch: expected " +
                      crypto::toHex(latestHash) + ", got " +
                      crypto::toHex(block.previousHash);
        spdlog::warn("[Blockchain] Block rejected: {}", lastError_);
        return false;
    }

    // --- Check 3: Block integrity ---
    if (!verifyBlock(block)) {
        lastError_ = "Block failed integrity verification (verifyBlock)";
        spdlog::warn("[Blockchain] Block rejected: {}", lastError_);
        return false;
    }

    // --- Check 4: PoA authority (if consensus is enabled) ---
    //
    // When a PoAConsensus is configured, the block's validator must be
    // the authority designated for this block index by the round-robin
    // schedule. A valid signature from the WRONG authority is rejected.
    if (consensus_ != nullptr) {
        if (!consensus_->validateBlock(block)) {
            lastError_ = "PoA violation: " + consensus_->getLastError();
            spdlog::warn("[Blockchain] Block rejected: {}", lastError_);
            return false;
        }
    }

    // --- All checks passed — accept the block ---
    chain_.push_back(block);

    spdlog::info("[Blockchain] Block {} accepted: {} ({} transactions)",
                 block.index,
                 crypto::toHex(block.hash),
                 block.transactions.size());

    return true;
}

const std::string& Blockchain::getLastError() const {
    return lastError_;
}

// =============================================================================
// Chain Validation
// =============================================================================

bool Blockchain::validateChain() const {
    if (chain_.empty()) {
        // This should never happen — the constructor always creates genesis.
        // But defensive programming is good practice.
        return false;
    }

    // --- Validate genesis block (Block 0) ---
    //
    // The genesis block has special properties:
    //   - index must be 0
    //   - previousHash must be all zeros
    const Block& genesis = chain_[0];
    if (genesis.index != 0) {
        return false;
    }

    Hash zeroHash{};
    if (genesis.previousHash != zeroHash) {
        return false;
    }

    if (!verifyBlock(genesis)) {
        return false;
    }

    // --- Validate every subsequent block ---
    //
    // For each block after genesis, check:
    //   1. Index is sequential
    //   2. previousHash links to the actual previous block
    //   3. Block passes integrity verification
    for (size_t i = 1; i < chain_.size(); ++i) {
        const Block& current = chain_[i];
        const Block& previous = chain_[i - 1];

        // Check sequential index
        if (current.index != i) {
            return false;
        }

        // Check chain linkage — THIS is the core blockchain property
        if (current.previousHash != previous.hash) {
            return false;
        }

        // Check block integrity
        if (!verifyBlock(current)) {
            return false;
        }
    }

    return true;
}

// =============================================================================
// Query Methods
// =============================================================================

const Block& Blockchain::getBlock(uint64_t index) const {
    if (index >= chain_.size()) {
        throw std::out_of_range(
            "Block index " + std::to_string(index) +
            " out of range (chain height: " + std::to_string(chain_.size()) + ")"
        );
    }
    return chain_[index];
}

const Block& Blockchain::getLatestBlock() const {
    // The chain always has at least the genesis block (set in constructor),
    // so back() is always safe.
    return chain_.back();
}

uint64_t Blockchain::getHeight() const {
    return chain_.size();
}

// =============================================================================
// Lookup Methods
// =============================================================================

const Block* Blockchain::findBlockByHash(const Hash& hash) const {
    // Linear scan through all blocks.
    // For V1 with small chains, this is perfectly adequate.
    //
    // Optimization for later: maintain an unordered_map<Hash, size_t>
    // that maps block hashes to their indices for O(1) lookup.
    for (const auto& block : chain_) {
        if (block.hash == hash) {
            return &block;
        }
    }
    return nullptr;  // Not found
}

const Transaction* Blockchain::findTransaction(const Hash& txHash) const {
    // Search every transaction in every block.
    //
    // This is O(N * M) where N = number of blocks, M = average
    // transactions per block. For V1 this is fine.
    //
    // Optimization for later: maintain an unordered_map<Hash, pair<size_t, size_t>>
    // mapping transaction hashes to (block index, transaction index).
    for (const auto& block : chain_) {
        for (const auto& tx : block.transactions) {
            if (tx.hash == txHash) {
                return &tx;
            }
        }
    }
    return nullptr;  // Not found
}

} // namespace core
} // namespace titancore
