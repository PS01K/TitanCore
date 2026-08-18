// =============================================================================
// TitanCore — Keys Module Tests
// =============================================================================
//
// These tests verify key generation, address derivation, and ECDSA
// signing/verification.
//
// TESTING STRATEGY:
//   Unlike hash tests where we can use known test vectors, key generation
//   involves randomness — we can't predict the output. Instead, we test
//   PROPERTIES that must hold true:
//
//   1. Generated keys have the correct sizes
//   2. Derived public keys match the key pair
//   3. Addresses have the correct size
//   4. Signing then verifying succeeds (round-trip)
//   5. Verification fails with the wrong key or tampered message
//   6. Different key pairs produce different keys (with overwhelming probability)
//
//   This approach is called "property-based testing" — we test invariants
//   rather than exact values.
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/crypto/keys.hpp"
#include "titancore/crypto/hash.hpp"

using namespace titancore;
using namespace titancore::crypto;

// =============================================================================
// Key Generation Tests
// =============================================================================

TEST(KeysTest, GenerateKeyPairProducesCorrectSizes) {
    KeyPair kp = generateKeyPair();

    // Private key: 32 bytes (256 bits)
    EXPECT_EQ(kp.privateKey.size(), 32);

    // Public key: 33 bytes (compressed: 1 prefix + 32 X-coordinate)
    EXPECT_EQ(kp.publicKey.size(), 33);
}

TEST(KeysTest, GeneratedPublicKeyHasValidPrefix) {
    KeyPair kp = generateKeyPair();

    // Compressed public keys start with 0x02 (even Y) or 0x03 (odd Y)
    uint8_t prefix = kp.publicKey[0];
    EXPECT_TRUE(prefix == 0x02 || prefix == 0x03);
}

TEST(KeysTest, PrivateKeyIsNotAllZeros) {
    KeyPair kp = generateKeyPair();

    // A private key of all zeros is invalid (it represents the point at infinity)
    PrivateKey zeros{};
    EXPECT_NE(kp.privateKey, zeros);
}

TEST(KeysTest, TwoKeyPairsAreDifferent) {
    // Two independently generated key pairs should be different.
    // The probability of collision is 1/2^256 — effectively impossible.
    KeyPair kp1 = generateKeyPair();
    KeyPair kp2 = generateKeyPair();

    EXPECT_NE(kp1.privateKey, kp2.privateKey);
    EXPECT_NE(kp1.publicKey, kp2.publicKey);
}

// =============================================================================
// Key Derivation Tests
// =============================================================================

TEST(KeysTest, DerivePublicKeyMatchesGenerated) {
    // The public key derived from the private key should match
    // the one generated alongside it.
    KeyPair kp = generateKeyPair();
    PublicKey derived = derivePublicKey(kp.privateKey);

    EXPECT_EQ(kp.publicKey, derived);
}

TEST(KeysTest, DerivePublicKeyIsDeterministic) {
    // Same private key should always produce the same public key.
    KeyPair kp = generateKeyPair();
    PublicKey derived1 = derivePublicKey(kp.privateKey);
    PublicKey derived2 = derivePublicKey(kp.privateKey);

    EXPECT_EQ(derived1, derived2);
}

// =============================================================================
// Address Derivation Tests
// =============================================================================

TEST(KeysTest, AddressIs20Bytes) {
    KeyPair kp = generateKeyPair();
    Address addr = deriveAddress(kp.publicKey);

    EXPECT_EQ(addr.size(), 20);
}

TEST(KeysTest, AddressIsDeterministic) {
    // Same public key should always produce the same address.
    KeyPair kp = generateKeyPair();
    Address addr1 = deriveAddress(kp.publicKey);
    Address addr2 = deriveAddress(kp.publicKey);

    EXPECT_EQ(addr1, addr2);
}

TEST(KeysTest, DifferentKeysProduceDifferentAddresses) {
    KeyPair kp1 = generateKeyPair();
    KeyPair kp2 = generateKeyPair();

    Address addr1 = deriveAddress(kp1.publicKey);
    Address addr2 = deriveAddress(kp2.publicKey);

    EXPECT_NE(addr1, addr2);
}

// =============================================================================
// ECDSA Signing & Verification Tests
// =============================================================================

TEST(KeysTest, SignProduces64ByteSignature) {
    KeyPair kp = generateKeyPair();
    Hash msgHash = sha256("test message");

    Signature sig = sign(msgHash, kp.privateKey);

    // Compact ECDSA signature: r (32 bytes) + s (32 bytes) = 64 bytes
    EXPECT_EQ(sig.size(), 64);
}

TEST(KeysTest, SignAndVerifyRoundTrip) {
    // The core property: sign with private key, verify with public key.
    KeyPair kp = generateKeyPair();
    Hash msgHash = sha256("Transfer 10 ODM from Alice to Bob");

    Signature sig = sign(msgHash, kp.privateKey);
    bool valid = verify(msgHash, sig, kp.publicKey);

    EXPECT_TRUE(valid);
}

TEST(KeysTest, VerifyFailsWithWrongPublicKey) {
    // A signature valid for key A should NOT verify with key B.
    // This ensures transactions can't be forged with a different key.
    KeyPair kpSigner = generateKeyPair();
    KeyPair kpOther = generateKeyPair();
    Hash msgHash = sha256("my transaction");

    Signature sig = sign(msgHash, kpSigner.privateKey);

    // Should verify with signer's key
    EXPECT_TRUE(verify(msgHash, sig, kpSigner.publicKey));

    // Should NOT verify with a different key
    EXPECT_FALSE(verify(msgHash, sig, kpOther.publicKey));
}

TEST(KeysTest, VerifyFailsWithTamperedMessage) {
    // If the message is changed after signing, verification must fail.
    // This ensures transaction data can't be modified after signing.
    KeyPair kp = generateKeyPair();
    Hash originalHash = sha256("Transfer 10 ODM");
    Hash tamperedHash = sha256("Transfer 1000 ODM");  // Attacker changes amount

    Signature sig = sign(originalHash, kp.privateKey);

    EXPECT_TRUE(verify(originalHash, sig, kp.publicKey));
    EXPECT_FALSE(verify(tamperedHash, sig, kp.publicKey));
}

TEST(KeysTest, VerifyFailsWithTamperedSignature) {
    // Corrupted signatures should be rejected.
    KeyPair kp = generateKeyPair();
    Hash msgHash = sha256("important data");

    Signature sig = sign(msgHash, kp.privateKey);

    // Flip a bit in the signature
    sig[0] ^= 0x01;

    EXPECT_FALSE(verify(msgHash, sig, kp.publicKey));
}

TEST(KeysTest, VerifyRejectsWrongSizeSignature) {
    // Signatures that aren't exactly 64 bytes should be rejected.
    KeyPair kp = generateKeyPair();
    Hash msgHash = sha256("data");

    Signature tooShort = {0x01, 0x02, 0x03};  // 3 bytes, not 64
    EXPECT_FALSE(verify(msgHash, tooShort, kp.publicKey));
}

TEST(KeysTest, SigningIsDeterministic) {
    // Because we use RFC 6979 (deterministic nonces), signing the
    // same message with the same key should always produce the same
    // signature. This is important for reproducibility and testing.
    KeyPair kp = generateKeyPair();
    Hash msgHash = sha256("deterministic signing test");

    Signature sig1 = sign(msgHash, kp.privateKey);
    Signature sig2 = sign(msgHash, kp.privateKey);

    EXPECT_EQ(sig1, sig2);
}

TEST(KeysTest, DifferentMessagesProduceDifferentSignatures) {
    KeyPair kp = generateKeyPair();
    Hash hash1 = sha256("message one");
    Hash hash2 = sha256("message two");

    Signature sig1 = sign(hash1, kp.privateKey);
    Signature sig2 = sign(hash2, kp.privateKey);

    EXPECT_NE(sig1, sig2);
}
