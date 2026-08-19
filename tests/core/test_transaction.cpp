// =============================================================================
// TitanCore — Transaction Tests
// =============================================================================
//
// These tests verify that transactions are correctly created, hashed,
// signed, verified, serialized, and that tampering is detected.
//
// TESTING STRATEGY:
//   We test the full transaction lifecycle:
//   1. Creation → correct fields, valid hash, valid signature
//   2. Verification → round-trip succeeds
//   3. Tampering detection → changing any field causes verification to fail
//   4. Serialization → JSON round-trip preserves all data
//   5. Edge cases → zero amount, address mismatch, etc.
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"

using namespace titancore;
using namespace titancore::core;
using namespace titancore::crypto;

// =============================================================================
// Test Fixture
// =============================================================================
// A test fixture provides shared setup code for multiple tests.
// GoogleTest creates a FRESH instance of this class for EACH test,
// so tests are completely isolated — one test can't affect another.
//
// TEST_F(TransactionTest, TestName) inherits from TransactionTest,
// so each test has access to sender, recipient, and a pre-built transaction.
// =============================================================================

class TransactionTest : public ::testing::Test {
protected:
    // These are initialized fresh for each test
    KeyPair senderKeys;
    KeyPair recipientKeys;
    Address recipientAddr;
    Transaction tx;

    void SetUp() override {
        // Generate fresh key pairs for each test
        senderKeys = generateKeyPair();
        recipientKeys = generateKeyPair();
        recipientAddr = deriveAddress(recipientKeys.publicKey);

        // Create a standard test transaction
        tx = createTransaction(senderKeys, recipientAddr, 100, 0);
    }
};

// =============================================================================
// Transaction Creation Tests
// =============================================================================

TEST_F(TransactionTest, CreateTransactionFillsAllFields) {
    // The sender address should be derived from the sender's public key
    Address expectedSender = deriveAddress(senderKeys.publicKey);
    EXPECT_EQ(tx.sender, expectedSender);

    // Recipient should match what we passed in
    EXPECT_EQ(tx.recipient, recipientAddr);

    // Amount and nonce should match
    EXPECT_EQ(tx.amount, 100);
    EXPECT_EQ(tx.nonce, 0);

    // Sender public key should match
    EXPECT_EQ(tx.senderPublicKey, senderKeys.publicKey);

    // Timestamp should be non-zero (set to current time)
    EXPECT_GT(tx.timestamp, 0);

    // Signature should be 64 bytes (compact ECDSA)
    EXPECT_EQ(tx.signature.size(), 64);

    // Hash should not be all zeros
    Hash zeroHash{};
    EXPECT_NE(tx.hash, zeroHash);
}

TEST_F(TransactionTest, HashIsDeterministic) {
    // Computing the hash of the same transaction should always
    // give the same result
    Hash hash1 = computeTransactionHash(tx);
    Hash hash2 = computeTransactionHash(tx);
    EXPECT_EQ(hash1, hash2);
}

TEST_F(TransactionTest, HashMatchesStoredHash) {
    // The stored hash should match what computeTransactionHash produces
    Hash recomputed = computeTransactionHash(tx);
    EXPECT_EQ(tx.hash, recomputed);
}

TEST_F(TransactionTest, DifferentTransactionsHaveDifferentHashes) {
    // Two transactions with different amounts should have different hashes
    Transaction tx2 = createTransaction(senderKeys, recipientAddr, 200, 0);
    EXPECT_NE(tx.hash, tx2.hash);
}

// =============================================================================
// Verification Tests
// =============================================================================

TEST_F(TransactionTest, ValidTransactionPassesVerification) {
    EXPECT_TRUE(verifyTransaction(tx));
}

TEST_F(TransactionTest, TamperedAmountFailsVerification) {
    // Simulate an attacker changing the amount after signing
    tx.amount = 999999;
    EXPECT_FALSE(verifyTransaction(tx));
}

TEST_F(TransactionTest, TamperedRecipientFailsVerification) {
    // Simulate an attacker redirecting funds to their own address
    KeyPair attacker = generateKeyPair();
    tx.recipient = deriveAddress(attacker.publicKey);
    EXPECT_FALSE(verifyTransaction(tx));
}

TEST_F(TransactionTest, TamperedNonceFailsVerification) {
    tx.nonce = 42;
    EXPECT_FALSE(verifyTransaction(tx));
}

TEST_F(TransactionTest, TamperedTimestampFailsVerification) {
    tx.timestamp = 0;
    EXPECT_FALSE(verifyTransaction(tx));
}

TEST_F(TransactionTest, WrongPublicKeyFailsVerification) {
    // If someone substitutes a different public key (e.g., trying to
    // make it look like a different sender), verification should fail
    // because the address won't match the public key.
    KeyPair otherKeys = generateKeyPair();
    tx.senderPublicKey = otherKeys.publicKey;
    EXPECT_FALSE(verifyTransaction(tx));
}

TEST_F(TransactionTest, TamperedSignatureFailsVerification) {
    // Corrupt the signature
    if (!tx.signature.empty()) {
        tx.signature[0] ^= 0xFF;
    }
    EXPECT_FALSE(verifyTransaction(tx));
}

TEST_F(TransactionTest, TransactionSignedByWrongKeyFailsVerification) {
    // Create a transaction but manually sign it with a different key.
    // The signature won't match the sender's public key.
    Transaction badTx;
    badTx.sender = deriveAddress(senderKeys.publicKey);
    badTx.recipient = recipientAddr;
    badTx.amount = 50;
    badTx.nonce = 0;
    badTx.timestamp = 1000;
    badTx.senderPublicKey = senderKeys.publicKey;

    // Sign with a DIFFERENT key (not the sender's)
    KeyPair wrongKeys = generateKeyPair();
    badTx.hash = computeTransactionHash(badTx);
    badTx.signature = sign(badTx.hash, wrongKeys.privateKey);

    EXPECT_FALSE(verifyTransaction(badTx));
}

// =============================================================================
// JSON Serialization Tests
// =============================================================================

TEST_F(TransactionTest, JsonRoundTrip) {
    // Serialize to JSON, then deserialize back — should produce
    // an identical transaction
    nlohmann::json j = toJson(tx);
    Transaction restored = fromJson(j);

    EXPECT_EQ(restored.sender, tx.sender);
    EXPECT_EQ(restored.recipient, tx.recipient);
    EXPECT_EQ(restored.amount, tx.amount);
    EXPECT_EQ(restored.nonce, tx.nonce);
    EXPECT_EQ(restored.timestamp, tx.timestamp);
    EXPECT_EQ(restored.senderPublicKey, tx.senderPublicKey);
    EXPECT_EQ(restored.signature, tx.signature);
    EXPECT_EQ(restored.hash, tx.hash);
}

TEST_F(TransactionTest, DeserializedTransactionIsStillValid) {
    // A transaction serialized and deserialized should still pass verification
    nlohmann::json j = toJson(tx);
    Transaction restored = fromJson(j);
    EXPECT_TRUE(verifyTransaction(restored));
}

TEST_F(TransactionTest, SignableJsonDoesNotContainSignatureOrHash) {
    // The signable JSON should NOT include signature or hash
    // (to avoid circular dependencies)
    nlohmann::json j = signableJson(tx);

    EXPECT_FALSE(j.contains("signature"));
    EXPECT_FALSE(j.contains("hash"));

    // But it should contain all signable fields
    EXPECT_TRUE(j.contains("sender"));
    EXPECT_TRUE(j.contains("recipient"));
    EXPECT_TRUE(j.contains("amount"));
    EXPECT_TRUE(j.contains("nonce"));
    EXPECT_TRUE(j.contains("timestamp"));
    EXPECT_TRUE(j.contains("senderPublicKey"));
}

TEST_F(TransactionTest, CompleteJsonContainsAllFields) {
    nlohmann::json j = toJson(tx);

    EXPECT_TRUE(j.contains("sender"));
    EXPECT_TRUE(j.contains("recipient"));
    EXPECT_TRUE(j.contains("amount"));
    EXPECT_TRUE(j.contains("nonce"));
    EXPECT_TRUE(j.contains("timestamp"));
    EXPECT_TRUE(j.contains("senderPublicKey"));
    EXPECT_TRUE(j.contains("signature"));
    EXPECT_TRUE(j.contains("hash"));
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(TransactionTest, ZeroAmountTransactionIsValid) {
    // A zero-amount transaction should still be cryptographically valid.
    // Whether it's economically valid (e.g., should we allow it?) is a
    // policy question for the state manager, not the transaction layer.
    Transaction zeroTx = createTransaction(senderKeys, recipientAddr, 0, 1);
    EXPECT_TRUE(verifyTransaction(zeroTx));
}

TEST_F(TransactionTest, SelfTransferIsValid) {
    // Sending to yourself is cryptographically valid (even if pointless).
    Address selfAddr = deriveAddress(senderKeys.publicKey);
    Transaction selfTx = createTransaction(senderKeys, selfAddr, 10, 1);
    EXPECT_TRUE(verifyTransaction(selfTx));
}

TEST_F(TransactionTest, DifferentNoncesProduceDifferentHashes) {
    // Two transactions identical except for the nonce should hash differently.
    // This is how the nonce prevents replay attacks — replayed transactions
    // have a different expected nonce and thus a different hash.
    Transaction tx1 = createTransaction(senderKeys, recipientAddr, 100, 0);
    Transaction tx2 = createTransaction(senderKeys, recipientAddr, 100, 1);
    EXPECT_NE(tx1.hash, tx2.hash);
}
