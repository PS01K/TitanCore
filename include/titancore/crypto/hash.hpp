#pragma once

// =============================================================================
// TitanCore — Cryptographic Hashing & Hex Utilities
// =============================================================================
//
// This module provides:
//   1. SHA-256 hashing — turning arbitrary data into a 32-byte fingerprint
//   2. Hex encoding  — converting raw bytes to human-readable hex strings
//   3. Hex decoding  — converting hex strings back to raw bytes
//
// WHY SHA-256?
//   SHA-256 (Secure Hash Algorithm, 256-bit) is used everywhere in blockchains:
//   - Block hashes: each block's identity is its SHA-256 hash
//   - Transaction hashes: each transaction has a unique hash (its "txid")
//   - Address derivation: public key → SHA-256 → truncate → address
//   - Merkle trees: efficiently proving a transaction is in a block
//
//   It's the same algorithm Bitcoin uses for block hashing.
//
// WHY HEX ENCODING?
//   Raw bytes are not human-readable. A 32-byte hash stored as uint8_t[32]
//   looks like garbage if you try to print it. Hex encoding converts each
//   byte to two hexadecimal characters:
//
//     byte 0xA1 → string "a1"
//     byte 0xFF → string "ff"
//     byte 0x00 → string "00"
//
//   So a 32-byte hash becomes a 64-character hex string like:
//     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
//
//   This is how you'll see hashes and addresses displayed in block explorers,
//   logs, and RPC responses.
// =============================================================================

#include "titancore/common/types.hpp"
#include <string>

namespace titancore {
namespace crypto {

// --- Hashing -----------------------------------------------------------------

// Compute the SHA-256 hash of arbitrary binary data.
// Returns a 32-byte Hash (std::array<uint8_t, 32>).
Hash sha256(const Bytes& data);

// Convenience overload: hash a string directly.
// Useful for testing (e.g., sha256("hello")) and for hashing
// serialized JSON strings.
Hash sha256(const std::string& data);

// Double SHA-256: sha256(sha256(data)).
// Used in Bitcoin for block hashing and transaction IDs.
// We include it because it's a common blockchain primitive.
Hash doubleSha256(const Bytes& data);

// --- Hex Encoding/Decoding ---------------------------------------------------

// Convert a fixed-size byte array to a lowercase hex string.
// Template so it works with Hash (32 bytes), Address (20 bytes),
// PublicKey (33 bytes), PrivateKey (32 bytes), etc.
//
// Example: Hash{0xa1, 0xb2, ...} → "a1b2..."
template<std::size_t N>
std::string toHex(const std::array<uint8_t, N>& data);

// Convert a variable-size byte vector to a lowercase hex string.
// Used for Signature and Bytes types.
std::string toHex(const Bytes& data);

// Convert a hex string back to raw bytes.
// Returns an empty vector if the input is not valid hex.
//
// Example: "a1b2" → Bytes{0xa1, 0xb2}
Bytes fromHex(const std::string& hex);

// =============================================================================
// Template Implementation
// =============================================================================
// Templates must be defined in the header because the compiler needs to see
// the full definition when instantiating the template for a specific type.
// This is a C++ language requirement — templates can't go in .cpp files
// (unless you explicitly instantiate every variant, which is cumbersome).
// =============================================================================

template<std::size_t N>
std::string toHex(const std::array<uint8_t, N>& data) {
    // Pre-allocate the exact size: 2 hex chars per byte
    std::string hex;
    hex.reserve(N * 2);

    // "0123456789abcdef" is a lookup table.
    // For byte 0xA1:
    //   high nibble = A (10) → "0123456789abcdef"[10] = 'a'
    //   low nibble  = 1     → "0123456789abcdef"[1]  = '1'
    //   result: "a1"
    static constexpr char hexChars[] = "0123456789abcdef";

    for (uint8_t byte : data) {
        hex.push_back(hexChars[(byte >> 4) & 0x0F]);  // High nibble
        hex.push_back(hexChars[byte & 0x0F]);          // Low nibble
    }

    return hex;
}

} // namespace crypto
} // namespace titancore
