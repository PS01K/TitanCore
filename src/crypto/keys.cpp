// =============================================================================
// TitanCore — Cryptographic Key Operations (Implementation)
// =============================================================================
//
// This file implements key generation, derivation, and ECDSA using:
//   - libsecp256k1 (Bitcoin Core) for elliptic curve operations
//   - OpenSSL's RAND_bytes() for cryptographically secure random numbers
//
// SECP256K1 CONTEXT
//   libsecp256k1 requires a "context" object for all operations. The context
//   pre-computes tables that accelerate elliptic curve math.
//
//   Creating a context is expensive (~1ms), so we create ONE static context
//   that lives for the entire program lifetime. This is safe because:
//   - secp256k1_context is thread-safe for signing and verification
//   - We only need one context for all operations
//
//   We use a function with a static local variable (the "Meyers Singleton"
//   pattern) to ensure the context is initialized exactly once, even in
//   multi-threaded code. C++11 guarantees thread-safe initialization of
//   function-local statics.
//
// RANDOMNESS
//   Private keys must be generated from cryptographically secure randomness.
//   Using weak randomness (like rand() or time-based seeds) would allow
//   attackers to guess private keys.
//
//   We use OpenSSL's RAND_bytes() which sources entropy from the OS:
//   - Linux: /dev/urandom (or getrandom syscall)
//   - macOS: /dev/urandom (backed by Fortuna CSPRNG)
//   - Windows: BCryptGenRandom
//
//   OpenSSL handles all the OS-specific details for us.
// =============================================================================

#include "titancore/crypto/keys.hpp"
#include "titancore/crypto/hash.hpp"

#include <secp256k1.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <cstring>

namespace titancore {
namespace crypto {

// --- secp256k1 Context (Singleton) -------------------------------------------

// Returns the global secp256k1 context, creating it on first call.
//
// SECP256K1_CONTEXT_NONE:
//   In secp256k1 v0.6.0, the context flags (SIGN, VERIFY) are no longer
//   needed — a single context can do everything. SECP256K1_CONTEXT_NONE
//   is the recommended way to create a context.
static secp256k1_context* getContext() {
    // "static local" — initialized exactly once, on first call.
    // The pointer lives until the program exits.
    static secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    return ctx;
}

// --- Key Generation ----------------------------------------------------------

KeyPair generateKeyPair() {
    KeyPair kp;
    secp256k1_context* ctx = getContext();

    // Step 1: Generate 32 bytes of cryptographically secure randomness.
    //
    // RAND_bytes() is OpenSSL's CSPRNG (Cryptographically Secure Pseudo-
    // Random Number Generator). It returns 1 on success.
    //
    // These 32 random bytes ARE the private key — a private key is just
    // a random number in the range [1, curve_order - 1].
    // The curve order is a ~256-bit prime, so almost any 32-byte random
    // value is a valid private key (probability of invalid ≈ 2^-128).
    if (RAND_bytes(kp.privateKey.data(), 32) != 1) {
        throw std::runtime_error("RAND_bytes failed to generate random bytes");
    }

    // Step 2: Validate that the random bytes form a valid private key.
    //
    // secp256k1_ec_seckey_verify() checks that the key is:
    //   - Not zero (the "point at infinity")
    //   - Less than the curve order
    //
    // If invalid (astronomically unlikely), regenerate.
    // In practice, you'd need to run this ~2^128 times to hit an invalid key.
    while (secp256k1_ec_seckey_verify(ctx, kp.privateKey.data()) != 1) {
        if (RAND_bytes(kp.privateKey.data(), 32) != 1) {
            throw std::runtime_error("RAND_bytes failed during key regeneration");
        }
    }

    // Step 3: Derive the public key from the private key.
    kp.publicKey = derivePublicKey(kp.privateKey);

    return kp;
}

// --- Key Derivation ----------------------------------------------------------

PublicKey derivePublicKey(const PrivateKey& privateKey) {
    secp256k1_context* ctx = getContext();

    // secp256k1_pubkey is the library's internal representation of a public key.
    // It's an opaque struct — you can't directly read the X/Y coordinates from it.
    // We need to serialize it to get the actual bytes.
    secp256k1_pubkey pubkey;

    // Perform elliptic curve point multiplication: public = private × G
    // This is the core mathematical operation of ECC.
    if (secp256k1_ec_pubkey_create(ctx, &pubkey, privateKey.data()) != 1) {
        throw std::runtime_error("Failed to derive public key (invalid private key?)");
    }

    // Serialize the public key in COMPRESSED format (33 bytes).
    //
    // SECP256K1_EC_COMPRESSED:
    //   Stores the X coordinate (32 bytes) + 1 prefix byte:
    //     0x02 if Y is even
    //     0x03 if Y is odd
    //
    // The alternative SECP256K1_EC_UNCOMPRESSED would be 65 bytes (0x04 + X + Y).
    // Compressed is preferred because it saves space with no loss of information.
    PublicKey result;
    size_t outputLen = result.size();  // 33

    if (secp256k1_ec_pubkey_serialize(
            ctx, result.data(), &outputLen, &pubkey,
            SECP256K1_EC_COMPRESSED) != 1) {
        throw std::runtime_error("Failed to serialize public key");
    }

    return result;
}

Address deriveAddress(const PublicKey& publicKey) {
    // Step 1: SHA-256 hash the public key
    //
    // We convert the fixed-size PublicKey (33 bytes) to a Bytes vector
    // because our sha256() function takes Bytes as input.
    Bytes pubKeyBytes(publicKey.begin(), publicKey.end());
    Hash pubKeyHash = sha256(pubKeyBytes);

    // Step 2: Take the LAST 20 bytes of the hash as the address.
    //
    // Why the last 20 bytes? This matches Ethereum's convention.
    // The choice of "last" vs "first" is arbitrary — what matters is
    // consistency. We pick a convention and stick with it.
    //
    // 20 bytes = 160 bits = 2^160 possible addresses ≈ 1.46 × 10^48
    // That's more than the number of atoms in a human body.
    Address address;
    std::copy(
        pubKeyHash.begin() + 12,   // Start at byte 12 (skip first 12)
        pubKeyHash.end(),          // End at byte 32
        address.begin()            // Write to address (20 bytes)
    );

    return address;
}

// --- Digital Signatures ------------------------------------------------------

Signature sign(const Hash& messageHash, const PrivateKey& privateKey) {
    secp256k1_context* ctx = getContext();

    // secp256k1_ecdsa_signature is the library's internal signature struct.
    secp256k1_ecdsa_signature sig;

    // Create the ECDSA signature.
    //
    // Parameters:
    //   ctx          - the secp256k1 context
    //   &sig         - output: the signature
    //   messageHash  - the 32-byte hash to sign
    //   privateKey   - the 32-byte private key
    //   NULL         - nonce function: NULL = use RFC 6979 (deterministic)
    //   NULL         - nonce data: not needed for RFC 6979
    //
    // RFC 6979:
    //   ECDSA requires a random nonce (one-time number) for each signature.
    //   If you reuse a nonce with the same private key, an attacker can
    //   RECOVER YOUR PRIVATE KEY (this is how the PS3 was hacked in 2010).
    //
    //   RFC 6979 solves this by generating the nonce DETERMINISTICALLY
    //   from the private key and message hash. Same inputs → same nonce,
    //   so no randomness needed, and reuse is impossible (different messages
    //   produce different nonces). libsecp256k1 uses this by default.
    if (secp256k1_ecdsa_sign(ctx, &sig, messageHash.data(),
                              privateKey.data(), nullptr, nullptr) != 1) {
        throw std::runtime_error("ECDSA signing failed");
    }

    // Serialize the signature in COMPACT format (64 bytes = r + s).
    //
    // compact format: [r (32 bytes)][s (32 bytes)]
    //   - r: the x-coordinate of a point computed during signing
    //   - s: a scalar computed from r, the hash, and the private key
    //
    // An alternative is DER format (70-72 bytes), which adds ASN.1 metadata.
    // We use compact because it's simpler and has a fixed size.
    Signature result(64);
    secp256k1_ecdsa_signature_serialize_compact(ctx, result.data(), &sig);

    return result;
}

bool verify(const Hash& messageHash, const Signature& signature,
            const PublicKey& publicKey) {
    // Input validation: compact signatures must be exactly 64 bytes
    if (signature.size() != 64) {
        return false;
    }

    secp256k1_context* ctx = getContext();

    // Step 1: Deserialize the compact signature back into the internal format.
    secp256k1_ecdsa_signature sig;
    if (secp256k1_ecdsa_signature_parse_compact(ctx, &sig, signature.data()) != 1) {
        return false;  // Malformed signature
    }

    // Step 2: Deserialize the compressed public key into the internal format.
    secp256k1_pubkey pubkey;
    if (secp256k1_ec_pubkey_parse(ctx, &pubkey,
                                   publicKey.data(), publicKey.size()) != 1) {
        return false;  // Malformed public key
    }

    // Step 3: Verify the signature.
    //
    // This checks the mathematical relationship:
    //   Does the signature (r, s) correspond to this hash being signed
    //   by the private key that produced this public key?
    //
    // Returns 1 if valid, 0 if invalid.
    return secp256k1_ecdsa_verify(ctx, &sig, messageHash.data(), &pubkey) == 1;
}

} // namespace crypto
} // namespace titancore
