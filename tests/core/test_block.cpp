// =============================================================================
// TitanCore — Block Tests
// =============================================================================
//
// These tests verify block creation, hashing, signing, verification,
// chain linkage, and tampering detection.
//
// TESTING STRATEGY:
//   1. Genesis block — correct initial state
//   2. Block creation — proper linkage to previous block
//   3. Verification — valid blocks pass, tampered blocks fail
//   4. Chain integrity — previousHash correctly links blocks
//   5. JSON round-trip — serialize/deserialize preserves all data
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/core/block.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"

using namespace titancore;
using namespace titancore::core;
using namespace titancore::crypto;

// =============================================================================
// Test Fixture
// =============================================================================

class BlockTest : public ::testing::Test {
protected:
    KeyPair validatorKeys;    // The authority node creating blocks
    KeyPair aliceKeys;        // A user (sender)
    KeyPair bobKeys;          // Another user (recipient)
    Address bobAddr;
    Block genesis;

    void SetUp() override {
        validatorKeys = generateKeyPair();
        aliceKeys = generateKeyPair();
        bobKeys = generateKeyPair();
        bobAddr = deriveAddress(bobKeys.publicKey);

        genesis = createGenesisBlock(validatorKeys);
    }

    // Helper: create a valid test transaction
    Transaction makeTestTx(uint64_t amount, uint64_t nonce) {
        return createTransaction(aliceKeys, bobAddr, amount, nonce);
    }
};

// =============================================================================
// Genesis Block Tests
// =============================================================================

TEST_F(BlockTest, GenesisBlockHasIndexZero) {
    EXPECT_EQ(genesis.index, 0);
}

TEST_F(BlockTest, GenesisBlockHasZeroPreviousHash) {
    // The genesis block's previousHash should be all zeros
    Hash zeroHash{};
    EXPECT_EQ(genesis.previousHash, zeroHash);
}

TEST_F(BlockTest, GenesisBlockHasFixedTimestamp) {
    // Genesis uses timestamp 0 (Unix epoch) so all nodes produce
    // the same genesis block
    EXPECT_EQ(genesis.timestamp, 0);
}

TEST_F(BlockTest, GenesisBlockHasNoTransactions) {
    EXPECT_TRUE(genesis.transactions.empty());
}

TEST_F(BlockTest, GenesisBlockHasValidHash) {
    Hash zeroHash{};
    EXPECT_NE(genesis.hash, zeroHash);
}

TEST_F(BlockTest, GenesisBlockHasValidSignature) {
    EXPECT_EQ(genesis.signature.size(), 64);
}

TEST_F(BlockTest, GenesisBlockPassesVerification) {
    EXPECT_TRUE(verifyBlock(genesis));
}

TEST_F(BlockTest, GenesisBlockHashIsDeterministic) {
    // Two genesis blocks from the same validator should have the same hash
    Block genesis2 = createGenesisBlock(validatorKeys);
    EXPECT_EQ(genesis.hash, genesis2.hash);
}

TEST_F(BlockTest, GenesisBlockHashDiffersPerValidator) {
    // Different validators produce different genesis blocks
    // (because the validator address/key is part of the header)
    KeyPair otherValidator = generateKeyPair();
    Block otherGenesis = createGenesisBlock(otherValidator);
    EXPECT_NE(genesis.hash, otherGenesis.hash);
}

// =============================================================================
// Block Creation Tests
// =============================================================================

TEST_F(BlockTest, NewBlockHasCorrectIndex) {
    Block block1 = createBlock(validatorKeys, genesis, {});
    EXPECT_EQ(block1.index, 1);

    Block block2 = createBlock(validatorKeys, block1, {});
    EXPECT_EQ(block2.index, 2);
}

TEST_F(BlockTest, NewBlockLinksToPrecessor) {
    Block block1 = createBlock(validatorKeys, genesis, {});

    // The new block's previousHash should be the genesis block's hash
    EXPECT_EQ(block1.previousHash, genesis.hash);
}

TEST_F(BlockTest, BlockContainsTransactions) {
    Transaction tx1 = makeTestTx(10, 0);
    Transaction tx2 = makeTestTx(20, 1);

    Block block = createBlock(validatorKeys, genesis, {tx1, tx2});

    EXPECT_EQ(block.transactions.size(), 2);
    EXPECT_EQ(block.transactions[0].amount, 10);
    EXPECT_EQ(block.transactions[1].amount, 20);
}

TEST_F(BlockTest, EmptyBlockIsValid) {
    // Blocks with no transactions should still be valid
    // (validators may produce empty blocks to maintain chain progress)
    Block emptyBlock = createBlock(validatorKeys, genesis, {});
    EXPECT_TRUE(verifyBlock(emptyBlock));
}

TEST_F(BlockTest, BlockWithTransactionsIsValid) {
    Transaction tx = makeTestTx(50, 0);
    Block block = createBlock(validatorKeys, genesis, {tx});
    EXPECT_TRUE(verifyBlock(block));
}

// =============================================================================
// Block Hash Tests
// =============================================================================

TEST_F(BlockTest, BlockHashIsDeterministic) {
    Hash hash1 = computeBlockHash(genesis);
    Hash hash2 = computeBlockHash(genesis);
    EXPECT_EQ(hash1, hash2);
}

TEST_F(BlockTest, BlockHashMatchesStoredHash) {
    Hash recomputed = computeBlockHash(genesis);
    EXPECT_EQ(genesis.hash, recomputed);
}

TEST_F(BlockTest, DifferentTransactionsProduceDifferentBlockHashes) {
    Transaction tx1 = makeTestTx(10, 0);
    Transaction tx2 = makeTestTx(20, 0);

    Block block1 = createBlock(validatorKeys, genesis, {tx1});
    Block block2 = createBlock(validatorKeys, genesis, {tx2});

    EXPECT_NE(block1.hash, block2.hash);
}

// =============================================================================
// Transaction Root Hash Tests
// =============================================================================

TEST_F(BlockTest, EmptyTransactionListHasConsistentRootHash) {
    std::vector<Transaction> empty;
    Hash root1 = computeTransactionRootHash(empty);
    Hash root2 = computeTransactionRootHash(empty);
    EXPECT_EQ(root1, root2);
}

TEST_F(BlockTest, TransactionOrderAffectsRootHash) {
    // The root hash should be different if transactions are reordered
    Transaction tx1 = makeTestTx(10, 0);
    Transaction tx2 = makeTestTx(20, 1);

    Hash root_12 = computeTransactionRootHash({tx1, tx2});
    Hash root_21 = computeTransactionRootHash({tx2, tx1});

    EXPECT_NE(root_12, root_21);
}

TEST_F(BlockTest, AddingTransactionChangesRootHash) {
    Transaction tx1 = makeTestTx(10, 0);
    Transaction tx2 = makeTestTx(20, 1);

    Hash root1 = computeTransactionRootHash({tx1});
    Hash root12 = computeTransactionRootHash({tx1, tx2});

    EXPECT_NE(root1, root12);
}

// =============================================================================
// Verification / Tampering Tests
// =============================================================================

TEST_F(BlockTest, TamperedIndexFailsVerification) {
    genesis.index = 99;
    EXPECT_FALSE(verifyBlock(genesis));
}

TEST_F(BlockTest, TamperedTimestampFailsVerification) {
    genesis.timestamp = 9999;
    EXPECT_FALSE(verifyBlock(genesis));
}

TEST_F(BlockTest, TamperedPreviousHashFailsVerification) {
    genesis.previousHash[0] ^= 0xFF;
    EXPECT_FALSE(verifyBlock(genesis));
}

TEST_F(BlockTest, TamperedValidatorFailsVerification) {
    // Change the validator address
    genesis.validator[0] ^= 0xFF;
    EXPECT_FALSE(verifyBlock(genesis));
}

TEST_F(BlockTest, WrongValidatorPublicKeyFailsVerification) {
    // Substitute a different validator's public key
    KeyPair imposter = generateKeyPair();
    genesis.validatorPublicKey = imposter.publicKey;
    EXPECT_FALSE(verifyBlock(genesis));
}

TEST_F(BlockTest, TamperedSignatureFailsVerification) {
    if (!genesis.signature.empty()) {
        genesis.signature[0] ^= 0xFF;
    }
    EXPECT_FALSE(verifyBlock(genesis));
}

TEST_F(BlockTest, ModifiedTransactionInBlockFailsVerification) {
    // Create a block with a valid transaction, then tamper with the transaction
    Transaction tx = makeTestTx(50, 0);
    Block block = createBlock(validatorKeys, genesis, {tx});

    EXPECT_TRUE(verifyBlock(block));

    // Tamper with the transaction amount
    block.transactions[0].amount = 999;

    // This should fail because:
    // 1. The transaction itself fails verification (hash mismatch)
    // 2. Even if we "fix" the tx hash, the block's transactionRootHash would change
    EXPECT_FALSE(verifyBlock(block));
}

TEST_F(BlockTest, AddedTransactionFailsVerification) {
    // Create a block with one transaction, then sneak in another
    Transaction tx1 = makeTestTx(10, 0);
    Block block = createBlock(validatorKeys, genesis, {tx1});

    // Add a transaction after signing
    Transaction tx2 = makeTestTx(20, 1);
    block.transactions.push_back(tx2);

    // Should fail: transactionRootHash changed → block hash changed
    EXPECT_FALSE(verifyBlock(block));
}

TEST_F(BlockTest, RemovedTransactionFailsVerification) {
    Transaction tx1 = makeTestTx(10, 0);
    Transaction tx2 = makeTestTx(20, 1);
    Block block = createBlock(validatorKeys, genesis, {tx1, tx2});

    // Remove a transaction after signing
    block.transactions.pop_back();

    // Should fail: transactionRootHash changed
    EXPECT_FALSE(verifyBlock(block));
}

// =============================================================================
// Chain Linkage Tests
// =============================================================================

TEST_F(BlockTest, ThreeBlockChainIsValid) {
    // Build a short chain and verify each block individually
    Transaction tx1 = makeTestTx(10, 0);
    Block block1 = createBlock(validatorKeys, genesis, {tx1});

    Transaction tx2 = makeTestTx(20, 1);
    Block block2 = createBlock(validatorKeys, block1, {tx2});

    EXPECT_TRUE(verifyBlock(genesis));
    EXPECT_TRUE(verifyBlock(block1));
    EXPECT_TRUE(verifyBlock(block2));

    // Verify chain linkage manually
    EXPECT_EQ(block1.previousHash, genesis.hash);
    EXPECT_EQ(block2.previousHash, block1.hash);
    EXPECT_EQ(block2.index, 2);
}

// =============================================================================
// JSON Serialization Tests
// =============================================================================

TEST_F(BlockTest, EmptyBlockJsonRoundTrip) {
    nlohmann::json j = toJson(genesis);
    Block restored = blockFromJson(j);

    EXPECT_EQ(restored.index, genesis.index);
    EXPECT_EQ(restored.timestamp, genesis.timestamp);
    EXPECT_EQ(restored.previousHash, genesis.previousHash);
    EXPECT_EQ(restored.validator, genesis.validator);
    EXPECT_EQ(restored.validatorPublicKey, genesis.validatorPublicKey);
    EXPECT_EQ(restored.signature, genesis.signature);
    EXPECT_EQ(restored.hash, genesis.hash);
    EXPECT_EQ(restored.transactions.size(), 0);
}

TEST_F(BlockTest, BlockWithTransactionsJsonRoundTrip) {
    Transaction tx = makeTestTx(50, 0);
    Block block = createBlock(validatorKeys, genesis, {tx});

    nlohmann::json j = toJson(block);
    Block restored = blockFromJson(j);

    EXPECT_EQ(restored.index, block.index);
    EXPECT_EQ(restored.hash, block.hash);
    EXPECT_EQ(restored.transactions.size(), 1);
    EXPECT_EQ(restored.transactions[0].amount, 50);
    EXPECT_EQ(restored.transactions[0].hash, tx.hash);
}

TEST_F(BlockTest, DeserializedBlockIsStillValid) {
    Transaction tx = makeTestTx(100, 0);
    Block block = createBlock(validatorKeys, genesis, {tx});

    nlohmann::json j = toJson(block);
    Block restored = blockFromJson(j);

    EXPECT_TRUE(verifyBlock(restored));
}

TEST_F(BlockTest, HeaderJsonDoesNotContainSignatureOrTransactions) {
    Transaction tx = makeTestTx(10, 0);
    Block block = createBlock(validatorKeys, genesis, {tx});

    nlohmann::json j = headerJson(block);

    // Header should NOT contain signature, hash, or full transactions
    EXPECT_FALSE(j.contains("signature"));
    EXPECT_FALSE(j.contains("hash"));
    EXPECT_FALSE(j.contains("transactions"));

    // But it should contain the transactionRootHash
    EXPECT_TRUE(j.contains("transactionRootHash"));
    EXPECT_TRUE(j.contains("index"));
    EXPECT_TRUE(j.contains("previousHash"));
    EXPECT_TRUE(j.contains("validator"));
}
