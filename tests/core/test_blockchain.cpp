// =============================================================================
// TitanCore — Blockchain Tests
// =============================================================================
//
// These tests verify the Blockchain class: initialization, block addition
// with validation, chain integrity, query methods, and rejection of
// invalid blocks.
//
// TESTING STRATEGY:
//   1. Construction — genesis block is correctly created
//   2. Block addition — valid blocks are accepted
//   3. Rejection — invalid blocks are rejected with correct reasons
//   4. Chain validation — validateChain detects tampering
//   5. Queries — getBlock, getLatestBlock, getHeight work correctly
//   6. Lookups — findBlockByHash, findTransaction find correct data
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/core/blockchain.hpp"
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

class BlockchainTest : public ::testing::Test {
protected:
    KeyPair validatorKeys;
    KeyPair aliceKeys;
    KeyPair bobKeys;
    Address bobAddr;

    void SetUp() override {
        validatorKeys = generateKeyPair();
        aliceKeys = generateKeyPair();
        bobKeys = generateKeyPair();
        bobAddr = deriveAddress(bobKeys.publicKey);
    }

    // Helper: create a valid test transaction
    Transaction makeTestTx(uint64_t amount, uint64_t nonce) {
        return createTransaction(aliceKeys, bobAddr, amount, nonce);
    }
};

// =============================================================================
// Construction Tests
// =============================================================================

TEST_F(BlockchainTest, ConstructionCreatesGenesisBlock) {
    Blockchain chain(validatorKeys);
    EXPECT_EQ(chain.getHeight(), 1);
}

TEST_F(BlockchainTest, GenesisBlockHasIndexZero) {
    Blockchain chain(validatorKeys);
    const Block& genesis = chain.getBlock(0);
    EXPECT_EQ(genesis.index, 0);
}

TEST_F(BlockchainTest, GenesisBlockHasZeroPreviousHash) {
    Blockchain chain(validatorKeys);
    const Block& genesis = chain.getBlock(0);
    Hash zeroHash{};
    EXPECT_EQ(genesis.previousHash, zeroHash);
}

TEST_F(BlockchainTest, GenesisBlockPassesVerification) {
    Blockchain chain(validatorKeys);
    const Block& genesis = chain.getBlock(0);
    EXPECT_TRUE(verifyBlock(genesis));
}

TEST_F(BlockchainTest, LatestBlockIsGenesisAfterConstruction) {
    Blockchain chain(validatorKeys);
    EXPECT_EQ(chain.getLatestBlock().index, 0);
}

// =============================================================================
// Adding Valid Blocks
// =============================================================================

TEST_F(BlockchainTest, AddValidEmptyBlock) {
    Blockchain chain(validatorKeys);

    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    EXPECT_TRUE(chain.addBlock(block1));
    EXPECT_EQ(chain.getHeight(), 2);
}

TEST_F(BlockchainTest, AddValidBlockWithTransactions) {
    Blockchain chain(validatorKeys);

    Transaction tx = makeTestTx(50, 0);
    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {tx});

    EXPECT_TRUE(chain.addBlock(block1));
    EXPECT_EQ(chain.getHeight(), 2);
    EXPECT_EQ(chain.getBlock(1).transactions.size(), 1);
}

TEST_F(BlockchainTest, AddMultipleBlocks) {
    Blockchain chain(validatorKeys);

    for (uint64_t i = 0; i < 5; ++i) {
        Transaction tx = makeTestTx(10 * (i + 1), i);
        Block block = createBlock(validatorKeys, chain.getLatestBlock(), {tx});
        EXPECT_TRUE(chain.addBlock(block));
    }

    EXPECT_EQ(chain.getHeight(), 6);  // genesis + 5 blocks
}

TEST_F(BlockchainTest, AddedBlockBecomesLatest) {
    Blockchain chain(validatorKeys);

    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    chain.addBlock(block1);

    EXPECT_EQ(chain.getLatestBlock().index, 1);
    EXPECT_EQ(chain.getLatestBlock().hash, block1.hash);
}

TEST_F(BlockchainTest, ChainLinkageIsCorrectAfterAdding) {
    Blockchain chain(validatorKeys);

    Hash genesisHash = chain.getBlock(0).hash;

    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    chain.addBlock(block1);

    // Block 1's previousHash should be genesis's hash
    EXPECT_EQ(chain.getBlock(1).previousHash, genesisHash);
}

// =============================================================================
// Rejection Tests — Wrong Index
// =============================================================================

TEST_F(BlockchainTest, RejectBlockWithWrongIndex_TooHigh) {
    Blockchain chain(validatorKeys);

    // Create block with index 1, then manually change it to 5
    Block block = createBlock(validatorKeys, chain.getLatestBlock(), {});
    block.index = 5;
    // Note: we don't re-sign because we want to test that the index
    // check happens BEFORE the integrity check (efficiency)

    EXPECT_FALSE(chain.addBlock(block));
    EXPECT_EQ(chain.getHeight(), 1);  // Chain unchanged
}

TEST_F(BlockchainTest, RejectBlockWithWrongIndex_Duplicate) {
    Blockchain chain(validatorKeys);

    // Add block 1 successfully
    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    EXPECT_TRUE(chain.addBlock(block1));

    // Try to add another block with index 1 (should need index 2)
    Block duplicate = createBlock(validatorKeys, chain.getBlock(0), {});
    EXPECT_FALSE(chain.addBlock(duplicate));
    EXPECT_EQ(chain.getHeight(), 2);  // Still just genesis + block1
}

TEST_F(BlockchainTest, RejectBlockWithWrongIndex_Zero) {
    Blockchain chain(validatorKeys);

    // Try to add a block with index 0 (genesis already exists)
    Block fakeGenesis = createGenesisBlock(validatorKeys);
    EXPECT_FALSE(chain.addBlock(fakeGenesis));
}

// =============================================================================
// Rejection Tests — Wrong Previous Hash
// =============================================================================

TEST_F(BlockchainTest, RejectBlockWithWrongPreviousHash) {
    Blockchain chain(validatorKeys);

    // Create a valid block but tamper with its previousHash
    Block block = createBlock(validatorKeys, chain.getLatestBlock(), {});
    block.previousHash[0] ^= 0xFF;

    EXPECT_FALSE(chain.addBlock(block));
    EXPECT_EQ(chain.getHeight(), 1);
}

TEST_F(BlockchainTest, RejectBlockLinkedToWrongPredecessor) {
    Blockchain chain(validatorKeys);

    // Add block 1 successfully
    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    chain.addBlock(block1);

    // Create block 2 but link it to genesis instead of block 1
    // (creating it from the genesis block gives it the wrong previousHash)
    Block badBlock2 = createBlock(validatorKeys, chain.getBlock(0), {});
    badBlock2.index = 2;  // Fix the index to be "correct"

    EXPECT_FALSE(chain.addBlock(badBlock2));
}

// =============================================================================
// Rejection Tests — Invalid Block
// =============================================================================

TEST_F(BlockchainTest, RejectBlockWithInvalidSignature) {
    Blockchain chain(validatorKeys);

    Block block = createBlock(validatorKeys, chain.getLatestBlock(), {});

    // Corrupt the signature
    if (!block.signature.empty()) {
        block.signature[0] ^= 0xFF;
    }

    EXPECT_FALSE(chain.addBlock(block));
    EXPECT_EQ(chain.getHeight(), 1);
}

TEST_F(BlockchainTest, RejectBlockWithTamperedTransaction) {
    Blockchain chain(validatorKeys);

    Transaction tx = makeTestTx(50, 0);
    Block block = createBlock(validatorKeys, chain.getLatestBlock(), {tx});

    // Tamper with the transaction amount after signing
    block.transactions[0].amount = 999;

    EXPECT_FALSE(chain.addBlock(block));
}

TEST_F(BlockchainTest, LastErrorIsSetOnRejection) {
    Blockchain chain(validatorKeys);

    Block fakeGenesis = createGenesisBlock(validatorKeys);
    chain.addBlock(fakeGenesis);

    EXPECT_FALSE(chain.getLastError().empty());
}

TEST_F(BlockchainTest, LastErrorIsClearedOnSuccess) {
    Blockchain chain(validatorKeys);

    // First, cause a rejection
    Block fakeGenesis = createGenesisBlock(validatorKeys);
    chain.addBlock(fakeGenesis);
    EXPECT_FALSE(chain.getLastError().empty());

    // Then add a valid block
    Block validBlock = createBlock(validatorKeys, chain.getLatestBlock(), {});
    chain.addBlock(validBlock);
    EXPECT_TRUE(chain.getLastError().empty());
}

// =============================================================================
// Chain Validation Tests
// =============================================================================

TEST_F(BlockchainTest, EmptyChainValidates) {
    // A chain with just genesis should validate
    Blockchain chain(validatorKeys);
    EXPECT_TRUE(chain.validateChain());
}

TEST_F(BlockchainTest, ChainWithMultipleBlocksValidates) {
    Blockchain chain(validatorKeys);

    for (uint64_t i = 0; i < 3; ++i) {
        Transaction tx = makeTestTx(10, i);
        Block block = createBlock(validatorKeys, chain.getLatestBlock(), {tx});
        chain.addBlock(block);
    }

    EXPECT_TRUE(chain.validateChain());
}

// =============================================================================
// Query Tests
// =============================================================================

TEST_F(BlockchainTest, GetBlockByIndex) {
    Blockchain chain(validatorKeys);

    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    chain.addBlock(block1);

    EXPECT_EQ(chain.getBlock(0).index, 0);
    EXPECT_EQ(chain.getBlock(1).index, 1);
}

TEST_F(BlockchainTest, GetBlockOutOfRangeThrows) {
    Blockchain chain(validatorKeys);
    EXPECT_THROW(chain.getBlock(99), std::out_of_range);
}

TEST_F(BlockchainTest, GetHeightReflectsAddedBlocks) {
    Blockchain chain(validatorKeys);
    EXPECT_EQ(chain.getHeight(), 1);

    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    chain.addBlock(block1);
    EXPECT_EQ(chain.getHeight(), 2);

    Block block2 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    chain.addBlock(block2);
    EXPECT_EQ(chain.getHeight(), 3);
}

// =============================================================================
// Lookup Tests
// =============================================================================

TEST_F(BlockchainTest, FindBlockByHashFindsExistingBlock) {
    Blockchain chain(validatorKeys);

    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    chain.addBlock(block1);

    const Block* found = chain.findBlockByHash(block1.hash);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->index, 1);
}

TEST_F(BlockchainTest, FindBlockByHashReturnsNullForNonexistent) {
    Blockchain chain(validatorKeys);

    Hash fakeHash{};
    fakeHash[0] = 0xDE;
    fakeHash[1] = 0xAD;

    EXPECT_EQ(chain.findBlockByHash(fakeHash), nullptr);
}

TEST_F(BlockchainTest, FindTransactionFindsExistingTx) {
    Blockchain chain(validatorKeys);

    Transaction tx = makeTestTx(75, 0);
    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {tx});
    chain.addBlock(block1);

    const Transaction* found = chain.findTransaction(tx.hash);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->amount, 75);
}

TEST_F(BlockchainTest, FindTransactionReturnsNullForNonexistent) {
    Blockchain chain(validatorKeys);

    Hash fakeTxHash{};
    fakeTxHash[0] = 0xBE;
    fakeTxHash[1] = 0xEF;

    EXPECT_EQ(chain.findTransaction(fakeTxHash), nullptr);
}

TEST_F(BlockchainTest, FindTransactionAcrossMultipleBlocks) {
    Blockchain chain(validatorKeys);

    // Create 3 blocks, each with a transaction
    Transaction tx1 = makeTestTx(10, 0);
    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {tx1});
    chain.addBlock(block1);

    Transaction tx2 = makeTestTx(20, 1);
    Block block2 = createBlock(validatorKeys, chain.getLatestBlock(), {tx2});
    chain.addBlock(block2);

    Transaction tx3 = makeTestTx(30, 2);
    Block block3 = createBlock(validatorKeys, chain.getLatestBlock(), {tx3});
    chain.addBlock(block3);

    // Should find all three transactions
    const Transaction* found1 = chain.findTransaction(tx1.hash);
    const Transaction* found2 = chain.findTransaction(tx2.hash);
    const Transaction* found3 = chain.findTransaction(tx3.hash);

    ASSERT_NE(found1, nullptr);
    ASSERT_NE(found2, nullptr);
    ASSERT_NE(found3, nullptr);

    EXPECT_EQ(found1->amount, 10);
    EXPECT_EQ(found2->amount, 20);
    EXPECT_EQ(found3->amount, 30);
}

// =============================================================================
// Multi-Validator Tests
// =============================================================================

TEST_F(BlockchainTest, DifferentValidatorsCanAddBlocks) {
    // In PoA, different authorities take turns creating blocks.
    // The chain should accept blocks from any validator (for now —
    // turn enforcement comes in a later milestone).
    Blockchain chain(validatorKeys);

    KeyPair validator2 = generateKeyPair();

    // Block 1 from validator 1
    Block block1 = createBlock(validatorKeys, chain.getLatestBlock(), {});
    EXPECT_TRUE(chain.addBlock(block1));

    // Block 2 from validator 2
    Block block2 = createBlock(validator2, chain.getLatestBlock(), {});
    EXPECT_TRUE(chain.addBlock(block2));

    EXPECT_EQ(chain.getHeight(), 3);
    EXPECT_TRUE(chain.validateChain());
}
