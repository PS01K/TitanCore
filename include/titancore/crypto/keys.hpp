#pragma once

// =============================================================================
// TitanCore — Cryptographic Key Operations
// =============================================================================
//
// This module handles everything related to elliptic curve cryptography:
//   1. Key pair generation (private key + public key)
//   2. Public key derivation from a private key
//   3. Address derivation from a public key
//   4. ECDSA signing (prove you own a private key)
//   5. ECDSA verification (check someone's signature is valid)
//
// THE SECP256K1 CURVE
//   All operations use the secp256k1 elliptic curve — the same curve used
//   by Bitcoin and Ethereum. The name breaks down as:
//     - "sec" = Standards for Efficient Cryptography
//     - "p"   = prime field (the math is done modulo a large prime)
//     - "256" = the prime is 256 bits long
//     - "k1"  = Koblitz curve variant 1
//
//   The curve equation is: y² = x³ + 7 (over a finite field)
//
// KEY PAIR RELATIONSHIP
//   Private Key → (elliptic curve multiplication) → Public Key
//
//   This is a ONE-WAY operation. Given a public key, there is no known
//   efficient algorithm to recover the private key. This "trapdoor" property
//   is what makes the entire system secure.
//
// ECDSA (Elliptic Curve Digital Signature Algorithm)
//   - sign(hash, private_key) → signature
//   - verify(hash, signature, public_key) → bool
//
//   The signer proves they know the private key WITHOUT revealing it.
//   Anyone with the public key can verify the signature is valid.
//
// WHY SIGN THE HASH, NOT THE MESSAGE?
//   ECDSA operates on fixed-size 32-byte inputs. Since messages can be
//   any size, we hash the message first (SHA-256 → 32 bytes), then sign
//   the hash. This is standard practice across all blockchain implementations.
// =============================================================================

#include "titancore/common/types.hpp"

namespace titancore {
namespace crypto {

// A key pair bundles a private key and its corresponding public key.
// Think of it like a username (public key / address) and password (private key).
struct KeyPair {
    PrivateKey privateKey;
    PublicKey  publicKey;
};

// ---- Key Generation ---------------------------------------------------------

// Generate a new random key pair.
//
// Internally:
//   1. Generates 32 bytes of cryptographically secure randomness
//      (using OpenSSL's RAND_bytes)
//   2. Validates the random bytes are a valid secp256k1 private key
//   3. Derives the corresponding public key via elliptic curve multiplication
//
// The private key should be kept secret. The public key can be shared freely.
KeyPair generateKeyPair();

// ---- Key Derivation ---------------------------------------------------------

// Derive a public key from an existing private key.
//
// This performs elliptic curve point multiplication:
//   public_key = private_key × G
// where G is the secp256k1 generator point (a well-known constant on the curve).
//
// The result is a 33-byte compressed public key:
//   byte 0:    0x02 or 0x03 (parity of the Y coordinate)
//   bytes 1-32: X coordinate
//
// WHY COMPRESSED?
//   An uncompressed public key stores both X and Y coordinates (65 bytes).
//   A compressed key stores only X + 1 parity bit (33 bytes). Since the
//   curve equation y² = x³ + 7 has exactly two solutions for Y given X
//   (one even, one odd), the parity bit tells you which one. This saves
//   32 bytes per public key with no loss of information.
PublicKey derivePublicKey(const PrivateKey& privateKey);

// Derive a blockchain address from a public key.
//
// Our address derivation:
//   1. Compute SHA-256(public_key) → 32 bytes
//   2. Take the last 20 bytes → address
//
// This is a simplified version of Ethereum's approach (which uses Keccak-256).
// The last 20 bytes give us a 160-bit address — enough for 2^160 unique
// addresses, which is astronomically more than we'll ever need.
Address deriveAddress(const PublicKey& publicKey);

// ---- Digital Signatures -----------------------------------------------------

// Sign a 32-byte message hash with a private key using ECDSA.
//
// Returns a 64-byte compact signature (r || s):
//   bytes 0-31:  r value (the x-coordinate of a curve point)
//   bytes 32-63: s value (computed from r, the hash, and the private key)
//
// The signature proves you know the private key without revealing it.
//
// NOTE: You must pass a HASH (32 bytes), not the raw message.
//       Always hash your message with sha256() first, then sign the hash.
//
// WHY COMPACT FORMAT?
//   ECDSA signatures can be encoded in two formats:
//   - DER format: variable length (70-72 bytes), includes metadata tags
//   - Compact format: fixed 64 bytes, just the raw r and s values
//
//   We use compact format because it's simpler and has a predictable size,
//   which makes serialization easier. Bitcoin uses DER; Ethereum uses compact.
Signature sign(const Hash& messageHash, const PrivateKey& privateKey);

// Verify an ECDSA signature against a message hash and public key.
//
// Returns true if the signature is valid:
//   - The signature was produced by the private key corresponding to publicKey
//   - The message hash has not been tampered with
//
// Returns false if:
//   - The signature doesn't match (wrong key, wrong message, or corrupted)
//   - The signature or public key is malformed
bool verify(const Hash& messageHash, const Signature& signature,
            const PublicKey& publicKey);

} // namespace crypto
} // namespace titancore
