// =============================================================================
// TitanCore — Block Implementation
// =============================================================================
//
// This file implements block creation, hashing, signing, verification,
// and the genesis block.
//
// BLOCK HASHING STRATEGY:
//   The block hash is computed from a JSON object containing the header fields
//   plus a "transactionRootHash" that commits to all transactions.
//
//   Block Header JSON → JSON string → SHA-256 → block hash
//
//   The transactionRootHash is computed by concatenating all transaction
//   hashes and hashing the result:
//
//     SHA-256(tx0.hash || tx1.hash || ... || txN.hash)
//
//   This means:
//   - Adding a transaction → different root → different block hash
//   - Removing a transaction → different root → different block hash
//   - Reordering transactions → different root → different block hash
//   - Modifying a transaction → its hash changes → different root → different block hash
//
// GENESIS BLOCK:
//   The genesis block uses a FIXED timestamp (0, representing Unix epoch)
//   rather than the current system time. This is critical: every node must
//   produce the exact same genesis block, byte-for-byte, so they compute
//   the same hash. If two nodes had different genesis hashes, they'd be
//   on different chains from the start.
// =============================================================================

#include "titancore/core/block.hpp"
#include "titancore/crypto/hash.hpp"

#include <chrono>
#include <stdexcept>

namespace titancore {
namespace core {

// =============================================================================
// Transaction Root Hash
// =============================================================================

Hash computeTransactionRootHash(const std::vector<Transaction>& transactions) {
    // Concatenate all transaction hashes into a single byte buffer.
    //
    // If we have 3 transactions with hashes H0, H1, H2 (each 32 bytes),
    // we create a buffer of 96 bytes: [H0 bytes][H1 bytes][H2 bytes]
    // and SHA-256 hash the entire buffer.
    //
    // Why include transaction ORDER? Because the concatenation is ordered,
    // [H0][H1] produces a different hash than [H1][H0]. This means the
    // root hash commits to the exact ordering of transactions in the block.
    Bytes combined;
    combined.reserve(transactions.size() * 32);  // 32 bytes per hash

    for (const auto& tx : transactions) {
        combined.insert(combined.end(), tx.hash.begin(), tx.hash.end());
    }

    // For an empty block (no transactions), this hashes an empty buffer.
    // SHA-256("") = e3b0c442... — a well-known constant.
    return crypto::sha256(combined);
}

// =============================================================================
// Block Header JSON (for hashing)
// =============================================================================

nlohmann::json headerJson(const Block& block) {
    // Build a JSON object with the header fields + transaction root hash.
    //
    // This is what gets hashed to produce the block's identity.
    // Like transactions, nlohmann::json sorts keys alphabetically,
    // giving us deterministic serialization.
    nlohmann::json j;

    j["index"]               = block.index;
    j["timestamp"]           = block.timestamp;
    j["previousHash"]        = crypto::toHex(block.previousHash);
    j["validator"]           = crypto::toHex(block.validator);
    j["validatorPublicKey"]  = crypto::toHex(block.validatorPublicKey);

    // Compute and include the transaction root hash.
    // This single 32-byte hash commits to ALL transactions in the block.
    Hash txRoot = computeTransactionRootHash(block.transactions);
    j["transactionRootHash"] = crypto::toHex(txRoot);

    return j;
}

// =============================================================================
// Block Hash
// =============================================================================

Hash computeBlockHash(const Block& block) {
    // Same pattern as transactions:
    // JSON header → string → SHA-256
    std::string serialized = headerJson(block).dump();
    return crypto::sha256(serialized);
}

// =============================================================================
// Block Signing
// =============================================================================

void signBlock(Block& block, const PrivateKey& validatorPrivateKey) {
    // Recompute hash to ensure it matches current header fields
    block.hash = computeBlockHash(block);

    // Sign the block hash with the validator's private key
    block.signature = crypto::sign(block.hash, validatorPrivateKey);
}

// =============================================================================
// Genesis Block
// =============================================================================

Block createGenesisBlock(const crypto::KeyPair& validatorKeyPair) {
    Block genesis;

    // --- Fixed header fields ---
    // These must be IDENTICAL on every node. Any difference means
    // a different genesis hash, which means a different chain.

    genesis.index = 0;

    // Fixed timestamp: Unix epoch (January 1, 1970 00:00:00 UTC).
    // We use 0 instead of the current time so every node produces
    // the exact same genesis block regardless of when it starts.
    genesis.timestamp = 0;

    // No previous block exists, so previousHash is all zeros.
    // This is the conventional "null hash" — it signals "I am the first block."
    genesis.previousHash = Hash{};  // Value-initialized to all zeros

    genesis.validator = crypto::deriveAddress(validatorKeyPair.publicKey);
    genesis.validatorPublicKey = validatorKeyPair.publicKey;

    // No transactions in the genesis block.
    // Initial ODM allocations will be handled by the State Manager
    // (a future milestone) when it processes the genesis block.
    genesis.transactions = {};

    // --- Compute hash and sign ---
    signBlock(genesis, validatorKeyPair.privateKey);

    return genesis;
}

// =============================================================================
// Block Creation
// =============================================================================

Block createBlock(
    const crypto::KeyPair& validatorKeyPair,
    const Block& previousBlock,
    const std::vector<Transaction>& transactions
) {
    Block block;

    // --- Header fields ---

    // The new block's index is one more than the previous block
    block.index = previousBlock.index + 1;

    // Current system time
    block.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    // THIS is the chain link. By storing the previous block's hash,
    // we cryptographically bind this block to all blocks before it.
    // Tampering with any ancestor block changes its hash, which
    // breaks this link.
    block.previousHash = previousBlock.hash;

    block.validator = crypto::deriveAddress(validatorKeyPair.publicKey);
    block.validatorPublicKey = validatorKeyPair.publicKey;

    // Copy the transactions into the block
    block.transactions = transactions;

    // --- Compute hash and sign ---
    signBlock(block, validatorKeyPair.privateKey);

    return block;
}

// =============================================================================
// Block Verification
// =============================================================================

bool verifyBlock(const Block& block) {
    // Check 1: Does the validator's public key derive to the validator address?
    //
    // Same logic as transaction verification — prevents someone from
    // claiming to be a different validator.
    Address derivedAddress = crypto::deriveAddress(block.validatorPublicKey);
    if (derivedAddress != block.validator) {
        return false;
    }

    // Check 2: Does the hash match the current header data?
    //
    // This detects tampering with any header field or any transaction.
    // If someone adds, removes, or modifies a transaction, the
    // transactionRootHash changes → the header hash changes.
    Hash recomputedHash = computeBlockHash(block);
    if (recomputedHash != block.hash) {
        return false;
    }

    // Check 3: Is the validator's signature valid?
    //
    // This proves the validator with this public key actually created
    // and signed this block.
    if (!crypto::verify(block.hash, block.signature, block.validatorPublicKey)) {
        return false;
    }

    // Check 4: Are all transactions in the block individually valid?
    //
    // A block with an invalid transaction is itself invalid.
    // This reuses verifyTransaction() from Milestone 2.
    for (const auto& tx : block.transactions) {
        if (!verifyTransaction(tx)) {
            return false;
        }
    }

    return true;
}

// =============================================================================
// JSON Serialization
// =============================================================================

// Helper: overload toJson/fromJson for blocks (separate from transaction versions)
// We use the block:: namespace prefix to avoid ambiguity with the transaction
// toJson/fromJson functions.

nlohmann::json toJson(const Block& block) {
    nlohmann::json j;

    j["index"]              = block.index;
    j["timestamp"]          = block.timestamp;
    j["previousHash"]       = crypto::toHex(block.previousHash);
    j["validator"]          = crypto::toHex(block.validator);
    j["validatorPublicKey"] = crypto::toHex(block.validatorPublicKey);
    j["signature"]          = crypto::toHex(block.signature);
    j["hash"]               = crypto::toHex(block.hash);

    // Serialize each transaction using the transaction's toJson()
    nlohmann::json txArray = nlohmann::json::array();
    for (const auto& tx : block.transactions) {
        // We need to qualify this call to use the transaction toJson,
        // not recurse into this function.
        txArray.push_back(titancore::core::toJson(tx));
    }
    j["transactions"] = txArray;

    return j;
}

Block blockFromJson(const nlohmann::json& j) {
    Block block;

    // Helper to decode hex into fixed-size arrays
    auto hexToArray = [](const std::string& hex, auto& dest) {
        Bytes bytes = crypto::fromHex(hex);
        if (bytes.size() != dest.size()) {
            throw std::runtime_error(
                "Invalid hex length: expected " + std::to_string(dest.size() * 2) +
                " chars, got " + std::to_string(hex.size())
            );
        }
        std::copy(bytes.begin(), bytes.end(), dest.begin());
    };

    block.index     = j.at("index").get<uint64_t>();
    block.timestamp = j.at("timestamp").get<uint64_t>();

    hexToArray(j.at("previousHash").get<std::string>(), block.previousHash);
    hexToArray(j.at("validator").get<std::string>(), block.validator);
    hexToArray(j.at("validatorPublicKey").get<std::string>(), block.validatorPublicKey);

    block.signature = crypto::fromHex(j.at("signature").get<std::string>());

    Bytes hashBytes = crypto::fromHex(j.at("hash").get<std::string>());
    if (hashBytes.size() != block.hash.size()) {
        throw std::runtime_error("Invalid block hash length in JSON");
    }
    std::copy(hashBytes.begin(), hashBytes.end(), block.hash.begin());

    // Deserialize transactions
    for (const auto& txJson : j.at("transactions")) {
        // This calls the Transaction deserialization function
        block.transactions.push_back(titancore::core::transactionFromJson(txJson));
    }

    return block;
}

} // namespace core
} // namespace titancore
