#pragma once

// =============================================================================
// TitanCore — Transaction
// =============================================================================
//
// A Transaction is the fundamental unit of action on the TitanCore blockchain.
// It represents a transfer of ODM from one address to another.
//
// LIFECYCLE OF A TRANSACTION:
//   1. User creates a transaction (sender, recipient, amount, nonce)
//   2. The "signable data" is serialized to JSON (deterministic byte order)
//   3. The serialized data is SHA-256 hashed → becomes the transaction hash (txid)
//   4. The hash is signed with the sender's private key → becomes the signature
//   5. The complete transaction is broadcast to the network
//   6. Validators verify: signature is valid, sender's public key matches address
//   7. Block producer includes the transaction in a block
//   8. State is updated: sender balance decreases, recipient balance increases
//
// IMPORTANT DESIGN DECISIONS:
//
//   1. The HASH covers the signable data only (not the signature).
//      If we included the signature in the hash, we'd have a circular
//      dependency: hash depends on signature, signature depends on hash.
//
//   2. We include the sender's PUBLIC KEY in the transaction.
//      Addresses are derived from public keys via one-way hashing, so you
//      can't reverse an address back to a public key. Validators need the
//      public key to verify the ECDSA signature, so the sender must provide it.
//
//   3. We use JSON for serialization (human-readable, debuggable).
//      Production blockchains use binary formats (RLP, Protobuf) for efficiency,
//      but JSON is much easier to understand and inspect during development.
//
//   4. Serialization MUST be deterministic.
//      The same transaction must always produce the same JSON string, which
//      produces the same hash. nlohmann/json's dump() sorts keys alphabetically
//      by default, giving us deterministic output.
// =============================================================================

#include "titancore/common/types.hpp"
#include "titancore/crypto/keys.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

namespace titancore {
namespace core {

// =============================================================================
// Transaction Structure
// =============================================================================

struct Transaction {
    // --- Signable fields (included in the hash) ------------------------------

    Address   sender;          // Who is sending ODM
    Address   recipient;       // Who is receiving ODM
    uint64_t  amount;          // How much ODM to transfer (in smallest units)
    uint64_t  nonce;           // Sender's transaction counter (replay protection)
    uint64_t  timestamp;       // Unix timestamp (seconds since epoch)
    PublicKey senderPublicKey;  // Sender's public key (for signature verification)

    // --- Computed fields (NOT part of the signable data) ----------------------

    Signature signature;       // ECDSA signature over the transaction hash
    Hash      hash;            // SHA-256 of the serialized signable data (txid)
};

// =============================================================================
// Transaction Operations
// =============================================================================

// Create a complete, signed transaction in one step.
//
// This is the main entry point for creating transactions. It:
//   1. Fills in all signable fields
//   2. Computes the transaction hash
//   3. Signs the hash with the sender's private key
//   4. Returns the fully formed transaction
//
// Parameters:
//   senderKeyPair - the sender's key pair (private key used for signing)
//   recipient     - the recipient's address
//   amount        - how much ODM to send
//   nonce         - the sender's current nonce (must match their account state)
Transaction createTransaction(
    const crypto::KeyPair& senderKeyPair,
    const Address& recipient,
    uint64_t amount,
    uint64_t nonce
);

// Compute the SHA-256 hash of a transaction's signable data.
//
// The "signable data" is everything EXCEPT the signature and hash fields:
//   sender, recipient, amount, nonce, timestamp, senderPublicKey
//
// This hash serves as:
//   - The transaction ID (txid) — a unique identifier
//   - The data that gets signed by ECDSA
Hash computeTransactionHash(const Transaction& tx);

// Sign an existing transaction (fills in the signature field).
//
// Prerequisite: the transaction's hash field must already be computed
// (call computeTransactionHash first, or use createTransaction which
// does both automatically).
void signTransaction(Transaction& tx, const PrivateKey& privateKey);

// Verify a transaction's integrity and authenticity.
//
// Checks three things:
//   1. The hash matches the signable data (data hasn't been tampered with)
//   2. The signature is valid for the hash + sender's public key
//   3. The sender's public key derives to the sender's address
//
// Returns true only if ALL three checks pass.
//
// NOTE: This does NOT check balances or nonces against blockchain state.
// That's the State Manager's job (a future milestone). This function only
// verifies the cryptographic integrity of the transaction itself.
bool verifyTransaction(const Transaction& tx);

// =============================================================================
// JSON Serialization
// =============================================================================
//
// We provide two JSON representations:
//
// 1. signableJson() — ONLY the signable fields. Used internally to compute
//    the hash. This must be deterministic (same input → same JSON → same hash).
//
// 2. toJson() — The COMPLETE transaction (all fields including signature
//    and hash). Used for network transmission and storage.
//
// 3. fromJson() — Reconstruct a Transaction from its complete JSON form.
// =============================================================================

// Get the JSON object containing only signable fields.
// This is what gets hashed to produce the transaction ID.
nlohmann::json signableJson(const Transaction& tx);

// Serialize the complete transaction to JSON (all fields).
nlohmann::json toJson(const Transaction& tx);

// Deserialize a transaction from its complete JSON representation.
Transaction transactionFromJson(const nlohmann::json& j);

} // namespace core
} // namespace titancore
