#pragma once

// =============================================================================
// TitanCore — Common Type Aliases
// =============================================================================
//
// This header defines type aliases that will be used across the entire project.
//
// WHY TYPE ALIASES?
//   Instead of writing std::array<uint8_t, 32> everywhere (which is verbose
//   and doesn't convey meaning), we create named types like Hash and Address.
//   This makes the code self-documenting:
//
//     Bad:  bool verify(std::vector<uint8_t> data, std::array<uint8_t, 32> hash);
//     Good: bool verify(Bytes data, Hash hash);
//
// WHY uint8_t?
//   Cryptographic data (hashes, keys, signatures) is raw binary data — just
//   bytes. uint8_t is an unsigned 8-bit integer (0-255), which represents
//   exactly one byte. std::array<uint8_t, 32> is therefore a fixed-size
//   array of 32 bytes = 256 bits, which is the output size of SHA-256.
//
// WHY std::array OVER std::vector?
//   - std::array has a FIXED size known at compile time (stack-allocated)
//   - std::vector has a DYNAMIC size (heap-allocated)
//   We use std::array for things with a known, fixed size (hashes are always
//   32 bytes, addresses are always 20 bytes) and std::vector for things
//   with variable size (transaction data, serialized blocks).
// =============================================================================

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace titancore {

// --- Fixed-size cryptographic types ------------------------------------------

// SHA-256 produces a 256-bit (32-byte) hash.
// Used for: block hashes, transaction hashes, Merkle roots.
using Hash = std::array<uint8_t, 32>;

// Blockchain addresses are 20 bytes (160 bits).
// This matches Ethereum's address format: the last 20 bytes of the
// Keccak-256 hash of the public key. We'll use a similar derivation.
using Address = std::array<uint8_t, 20>;

// A secp256k1 public key in compressed form is 33 bytes.
// "Compressed" means we store only the X coordinate + a parity bit,
// rather than both X and Y coordinates (which would be 65 bytes).
using PublicKey = std::array<uint8_t, 33>;

// A secp256k1 private key is 32 bytes (256 bits) — a random number
// in the range [1, curve_order - 1].
using PrivateKey = std::array<uint8_t, 32>;

// ECDSA signatures consist of two 32-byte integers (r, s) plus a
// recovery byte, totaling up to 65 bytes. However, DER-encoded
// signatures (which secp256k1 produces) have variable length (70-72 bytes).
// We use a vector for flexibility.
using Signature = std::vector<uint8_t>;

// --- Variable-size types -----------------------------------------------------

// Raw byte buffer for arbitrary binary data.
// Used for: serialized transactions, serialized blocks, network messages.
using Bytes = std::vector<uint8_t>;

} // namespace titancore
