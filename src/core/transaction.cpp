// =============================================================================
// TitanCore — Transaction Implementation
// =============================================================================
//
// This file implements transaction creation, hashing, signing, verification,
// and JSON serialization.
//
// THE SERIALIZATION PROBLEM
//   To compute a transaction hash, we need to convert the transaction's
//   signable fields into a deterministic byte sequence. We use JSON for this:
//
//     Transaction fields → JSON object → JSON string → SHA-256 → hash
//
//   The critical requirement is DETERMINISM: the same transaction must always
//   produce the exact same JSON string, byte-for-byte. If Node A produces
//   a different JSON string than Node B for the same transaction, they'll
//   compute different hashes and disagree about the transaction's identity.
//
//   nlohmann/json's dump() method serializes with keys in alphabetical order
//   by default (because it uses std::map internally). This gives us
//   deterministic output without extra effort.
//
// HEX ENCODING IN JSON
//   Binary data (addresses, public keys, signatures, hashes) can't be
//   directly represented in JSON (which is a text format). We encode all
//   binary fields as lowercase hex strings in the JSON representation.
//
//   Example: Address{0xab, 0xcd, ...} → "abcd..."
// =============================================================================

#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"

#include <chrono>
#include <stdexcept>

namespace titancore {
namespace core {

// =============================================================================
// JSON Serialization (defined first because other functions use it)
// =============================================================================

nlohmann::json signableJson(const Transaction& tx) {
    // Build a JSON object with ONLY the signable fields.
    // These are the fields that contribute to the transaction hash.
    //
    // All binary data is hex-encoded because JSON is a text format.
    //
    // nlohmann/json uses std::map under the hood, which sorts keys
    // alphabetically. This means the JSON output is always:
    //   {"amount":..., "nonce":..., "recipient":..., "sender":...,
    //    "senderPublicKey":..., "timestamp":...}
    //
    // This alphabetical ordering is what makes our serialization deterministic.
    nlohmann::json j;

    j["sender"]          = crypto::toHex(tx.sender);
    j["recipient"]       = crypto::toHex(tx.recipient);
    j["amount"]          = tx.amount;
    j["nonce"]           = tx.nonce;
    j["timestamp"]       = tx.timestamp;
    j["senderPublicKey"] = crypto::toHex(tx.senderPublicKey);

    return j;
}

nlohmann::json toJson(const Transaction& tx) {
    // Start with the signable fields
    nlohmann::json j = signableJson(tx);

    // Add the computed fields (signature and hash)
    j["signature"] = crypto::toHex(tx.signature);
    j["hash"]      = crypto::toHex(tx.hash);

    return j;
}

Transaction fromJson(const nlohmann::json& j) {
    Transaction tx;

    // --- Decode signable fields ---

    // fromHex returns a Bytes (vector), but sender/recipient/publicKey are
    // fixed-size arrays. We need to copy the bytes into the array.
    //
    // This helper lambda converts a hex string to a fixed-size std::array.
    // It validates the size matches what we expect.
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

    hexToArray(j.at("sender").get<std::string>(), tx.sender);
    hexToArray(j.at("recipient").get<std::string>(), tx.recipient);
    hexToArray(j.at("senderPublicKey").get<std::string>(), tx.senderPublicKey);

    tx.amount    = j.at("amount").get<uint64_t>();
    tx.nonce     = j.at("nonce").get<uint64_t>();
    tx.timestamp = j.at("timestamp").get<uint64_t>();

    // --- Decode computed fields ---

    tx.signature = crypto::fromHex(j.at("signature").get<std::string>());

    Bytes hashBytes = crypto::fromHex(j.at("hash").get<std::string>());
    if (hashBytes.size() != tx.hash.size()) {
        throw std::runtime_error("Invalid hash length in JSON");
    }
    std::copy(hashBytes.begin(), hashBytes.end(), tx.hash.begin());

    return tx;
}

// =============================================================================
// Transaction Hash
// =============================================================================

Hash computeTransactionHash(const Transaction& tx) {
    // Step 1: Serialize the signable fields to a JSON string.
    //
    // dump() converts the JSON object to a string. The default behavior
    // is no indentation (compact), which is what we want for hashing.
    //
    // Because nlohmann::json uses std::map (sorted keys), this string
    // is DETERMINISTIC — the same fields always produce the same string.
    std::string serialized = signableJson(tx).dump();

    // Step 2: SHA-256 hash the serialized string.
    //
    // This hash IS the transaction ID (txid). It uniquely identifies this
    // specific transaction. Even a 1-bit change in any field produces a
    // completely different hash (the avalanche effect from Milestone 1).
    return crypto::sha256(serialized);
}

// =============================================================================
// Transaction Signing
// =============================================================================

void signTransaction(Transaction& tx, const PrivateKey& privateKey) {
    // The hash must be computed before signing.
    // We recompute it here to ensure consistency — even if the caller
    // already computed it, this guarantees the hash matches the current fields.
    tx.hash = computeTransactionHash(tx);

    // Sign the transaction hash with the sender's private key.
    // This produces a 64-byte compact ECDSA signature (r || s).
    //
    // After this, anyone with the sender's public key can verify:
    //   1. The sender actually authorized this transaction
    //   2. No field has been modified since signing
    tx.signature = crypto::sign(tx.hash, privateKey);
}

// =============================================================================
// Transaction Creation (convenience function)
// =============================================================================

Transaction createTransaction(
    const crypto::KeyPair& senderKeyPair,
    const Address& recipient,
    uint64_t amount,
    uint64_t nonce
) {
    Transaction tx;

    // --- Fill in the signable fields ---

    tx.senderPublicKey = senderKeyPair.publicKey;

    // Derive the sender's address from their public key.
    // This ensures the sender field is always consistent with the public key.
    tx.sender = crypto::deriveAddress(senderKeyPair.publicKey);

    tx.recipient = recipient;
    tx.amount    = amount;
    tx.nonce     = nonce;

    // Use the current system time as the timestamp.
    //
    // std::chrono::system_clock::now() returns the current time.
    // time_since_epoch() gives the duration since Unix epoch (Jan 1, 1970).
    // We cast to seconds for a simple integer timestamp.
    //
    // NOTE: In a distributed system, clocks on different nodes may differ
    // slightly. For V1 this is fine — timestamps are informational, not
    // used for consensus. PoA uses block index for turn ordering, not time.
    tx.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    // --- Compute hash and sign ---

    signTransaction(tx, senderKeyPair.privateKey);

    return tx;
}

// =============================================================================
// Transaction Verification
// =============================================================================

bool verifyTransaction(const Transaction& tx) {
    // Check 1: Does the sender's public key derive to the sender's address?
    //
    // This prevents someone from claiming to be Alice (using Alice's address)
    // while providing their own public key (and signing with their own key).
    // Without this check, anyone could send transactions "from" any address.
    Address derivedAddress = crypto::deriveAddress(tx.senderPublicKey);
    if (derivedAddress != tx.sender) {
        return false;  // Public key doesn't match the claimed sender
    }

    // Check 2: Does the hash match the transaction's signable data?
    //
    // This detects tampering. If someone changed the amount from 10 to 1000
    // after the transaction was signed, the recomputed hash won't match.
    Hash recomputedHash = computeTransactionHash(tx);
    if (recomputedHash != tx.hash) {
        return false;  // Transaction data has been tampered with
    }

    // Check 3: Is the ECDSA signature valid?
    //
    // This proves the owner of the private key corresponding to
    // senderPublicKey actually signed this transaction hash.
    return crypto::verify(tx.hash, tx.signature, tx.senderPublicKey);
}

} // namespace core
} // namespace titancore
