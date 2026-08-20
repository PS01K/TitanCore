#pragma once

// =============================================================================
// TitanCore — Block
// =============================================================================
//
// A Block is a container that groups transactions into an ordered, tamper-proof
// unit. Blocks are linked together via the previousHash field, forming the
// "chain" in blockchain.
//
// BLOCK STRUCTURE:
//   A block has two logical parts:
//
//   1. HEADER — metadata about the block itself:
//      index, timestamp, previousHash, validator, validatorPublicKey,
//      transactionRootHash
//
//   2. BODY — the actual payload:
//      a vector of Transaction objects
//
//   The block HASH covers only the header (plus the transactionRootHash,
//   which commits to the body). This way the hash is a fixed size regardless
//   of how many transactions are in the block.
//
// CHAIN LINKAGE:
//   Block 0 (genesis) → Block 1 → Block 2 → ...
//
//   Each block stores the hash of the previous block. If any block is
//   tampered with, its hash changes, which breaks the link from the next
//   block. To tamper successfully, you'd need to recalculate every
//   subsequent block — and each one is signed by a different authority.
//
// TRANSACTION ROOT HASH:
//   Instead of putting all transaction data into the block header (which
//   could be huge), we compute a "transaction root hash":
//
//     transactionRootHash = SHA-256(tx0.hash || tx1.hash || tx2.hash || ...)
//
//   where || means concatenation. This is a simplified Merkle root.
//   If any transaction is changed, added, or removed, the root hash changes,
//   which changes the block hash, which breaks the chain.
//
// GENESIS BLOCK:
//   The very first block (index 0). It has:
//   - previousHash: all zeros (no previous block exists)
//   - transactions: empty (or initial allocations)
//   - It's hardcoded, not computed — every node starts with the same one
// =============================================================================

#include "titancore/common/types.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/keys.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <vector>

namespace titancore {
namespace core {

// =============================================================================
// Block Structure
// =============================================================================

struct Block {
    // --- Header fields (included in the block hash) --------------------------

    uint64_t  index;               // Block number (0 = genesis, 1, 2, ...)
    uint64_t  timestamp;           // When the block was created (Unix seconds)
    Hash      previousHash;        // Hash of the previous block (chain link)
    Address   validator;           // Address of the authority that created this block
    PublicKey validatorPublicKey;   // Authority's public key (for signature verification)

    // --- Body ----------------------------------------------------------------

    std::vector<Transaction> transactions;  // The batch of transactions

    // --- Computed fields (NOT part of the header hash) ------------------------

    Signature signature;           // Authority's ECDSA signature over the block hash
    Hash      hash;                // SHA-256 of the serialized header (block's identity)
};

// =============================================================================
// Block Operations
// =============================================================================

// Compute the "transaction root hash" for a list of transactions.
//
// This is a simplified Merkle root:
//   SHA-256(tx0.hash || tx1.hash || tx2.hash || ...)
//
// For an empty transaction list, returns SHA-256 of an empty byte sequence.
//
// A real Merkle tree would hash pairs recursively to form a binary tree,
// allowing efficient proofs that a specific transaction is in the block.
// Our simplified version provides the same tamper-detection property
// without the proof capability. We may upgrade to a full Merkle tree later.
Hash computeTransactionRootHash(const std::vector<Transaction>& transactions);

// Compute the SHA-256 hash of a block's header data.
//
// The header hash covers:
//   index, timestamp, previousHash, validator, validatorPublicKey,
//   and the transactionRootHash (which commits to all transactions)
//
// The block's signature and hash fields are NOT included (same reason
// as transactions — avoiding circular dependencies).
Hash computeBlockHash(const Block& block);

// Sign a block (fills in the signature field).
// Also recomputes the block hash to ensure consistency.
void signBlock(Block& block, const PrivateKey& validatorPrivateKey);

// Create the genesis block (Block 0).
//
// Parameters:
//   validatorKeyPair - the authority that "creates" the genesis block
//
// The genesis block has:
//   - index: 0
//   - previousHash: all zeros (32 bytes of 0x00)
//   - transactions: empty
//   - timestamp: a fixed value (so all nodes produce the same genesis)
Block createGenesisBlock(const crypto::KeyPair& validatorKeyPair);

// Create a new block with transactions.
//
// Parameters:
//   validatorKeyPair - the authority creating this block
//   previousBlock    - the block this new block follows (for index + previousHash)
//   transactions     - the transactions to include
//
// The new block will have:
//   - index: previousBlock.index + 1
//   - previousHash: previousBlock.hash
//   - Current timestamp
//   - Signed by the validator
Block createBlock(
    const crypto::KeyPair& validatorKeyPair,
    const Block& previousBlock,
    const std::vector<Transaction>& transactions
);

// Verify a block's integrity and authenticity.
//
// Checks:
//   1. Block hash matches recomputed header hash
//   2. Validator's public key derives to the validator address
//   3. Signature is valid (signed by the validator's private key)
//   4. All transactions in the block are individually valid
//
// NOTE: This does NOT verify chain linkage (previousHash matching the actual
// previous block). That's the Blockchain's job (a future milestone).
// This function only validates the block in isolation.
bool verifyBlock(const Block& block);

// =============================================================================
// JSON Serialization
// =============================================================================

// Serialize the block header fields to JSON (for hashing).
// Does NOT include signature, hash, or full transaction data —
// only the transactionRootHash.
nlohmann::json headerJson(const Block& block);

// Serialize the complete block to JSON (all fields + full transactions).
nlohmann::json toJson(const Block& block);

// Deserialize a block from its complete JSON representation.
Block blockFromJson(const nlohmann::json& j);

} // namespace core
} // namespace titancore
