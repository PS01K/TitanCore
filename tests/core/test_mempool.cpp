// =============================================================================
// TitanCore — Mempool Tests
// =============================================================================
//
// These tests verify the Mempool: transaction acceptance, rejection,
// pending state tracking, block creation support, post-mining cleanup,
// and re-validation after state changes.
//
// TESTING STRATEGY:
//   1. Acceptance — valid transactions are stored
//   2. Rejection — invalid transactions are rejected with correct reasons
//   3. Pending state — multiple sequential transactions from same sender
//   4. Block creation — transactions ordered by nonce per sender
//   5. Post-mining — mined transactions removed, state re-validated
//   6. Edge cases — duplicates, unknown senders, empty mempool
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/core/mempool.hpp"
#include "titancore/core/state.hpp"
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

class MempoolTest : public ::testing::Test {
protected:
    KeyPair validatorKeys;
    KeyPair aliceKeys;
    KeyPair bobKeys;
    KeyPair carolKeys;
    Address aliceAddr;
    Address bobAddr;
    Address carolAddr;
    Block genesis;

    void SetUp() override {
        validatorKeys = generateKeyPair();
        aliceKeys = generateKeyPair();
        bobKeys = generateKeyPair();
        carolKeys = generateKeyPair();
        aliceAddr = deriveAddress(aliceKeys.publicKey);
        bobAddr = deriveAddress(bobKeys.publicKey);
        carolAddr = deriveAddress(carolKeys.publicKey);

        genesis = createGenesisBlock(validatorKeys);
    }

    // Helper: create a StateManager with Alice and Bob having 10000 ODM each
    StateManager makeDefaultState() {
        AddressMap<uint64_t> allocations;
        allocations[aliceAddr] = 10000;
        allocations[bobAddr] = 10000;
        return StateManager(allocations);
    }

    // Helper: create a block with transactions
    Block makeBlock(const Block& prev, const std::vector<Transaction>& txs) {
        return createBlock(validatorKeys, prev, txs);
    }
};

// =============================================================================
// Basic Acceptance Tests
// =============================================================================

TEST_F(MempoolTest, AcceptValidTransaction) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
    EXPECT_TRUE(pool.addTransaction(tx));
    EXPECT_EQ(pool.size(), 1);
}

TEST_F(MempoolTest, AcceptMultipleTransactionsFromDifferentSenders) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    Transaction tx2 = createTransaction(bobKeys, aliceAddr, 200, 0);

    EXPECT_TRUE(pool.addTransaction(tx1));
    EXPECT_TRUE(pool.addTransaction(tx2));
    EXPECT_EQ(pool.size(), 2);
}

TEST_F(MempoolTest, ContainsReturnsTrueForPendingTx) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
    pool.addTransaction(tx);

    EXPECT_TRUE(pool.contains(tx.hash));
}

TEST_F(MempoolTest, ContainsReturnsFalseForUnknownTx) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Hash fakeHash{};
    fakeHash[0] = 0xDE;
    EXPECT_FALSE(pool.contains(fakeHash));
}

// =============================================================================
// Rejection Tests
// =============================================================================

TEST_F(MempoolTest, RejectDuplicateTransaction) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
    EXPECT_TRUE(pool.addTransaction(tx));
    EXPECT_FALSE(pool.addTransaction(tx));  // Duplicate
    EXPECT_EQ(pool.size(), 1);
}

TEST_F(MempoolTest, RejectInvalidSignature) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
    // Corrupt the signature
    if (!tx.signature.empty()) {
        tx.signature[0] ^= 0xFF;
    }

    EXPECT_FALSE(pool.addTransaction(tx));
    EXPECT_EQ(pool.size(), 0);
}

TEST_F(MempoolTest, RejectUnknownSender) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    // Carol has no account
    Transaction tx = createTransaction(carolKeys, bobAddr, 100, 0);
    EXPECT_FALSE(pool.addTransaction(tx));
}

TEST_F(MempoolTest, RejectWrongNonce) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    // Alice's nonce is 0, but we submit nonce 5
    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 5);
    EXPECT_FALSE(pool.addTransaction(tx));
}

TEST_F(MempoolTest, RejectInsufficientBalance) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    // Alice has 10000 but tries to send 99999
    Transaction tx = createTransaction(aliceKeys, bobAddr, 99999, 0);
    EXPECT_FALSE(pool.addTransaction(tx));
}

TEST_F(MempoolTest, LastErrorIsSetOnRejection) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx = createTransaction(aliceKeys, bobAddr, 99999, 0);
    pool.addTransaction(tx);

    EXPECT_FALSE(pool.getLastError().empty());
}

TEST_F(MempoolTest, LastErrorIsClearedOnSuccess) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    // Cause a rejection first
    Transaction badTx = createTransaction(aliceKeys, bobAddr, 99999, 0);
    pool.addTransaction(badTx);
    EXPECT_FALSE(pool.getLastError().empty());

    // Then succeed
    Transaction goodTx = createTransaction(aliceKeys, bobAddr, 100, 0);
    pool.addTransaction(goodTx);
    EXPECT_TRUE(pool.getLastError().empty());
}

// =============================================================================
// Pending State Tests
// =============================================================================

TEST_F(MempoolTest, SequentialNoncesFromSameSender) {
    // This is the KEY feature of pending state tracking.
    // Alice submits nonce 0, then nonce 1 — both should be accepted.
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 200, 1);
    Transaction tx3 = createTransaction(aliceKeys, bobAddr, 300, 2);

    EXPECT_TRUE(pool.addTransaction(tx1));
    EXPECT_TRUE(pool.addTransaction(tx2));
    EXPECT_TRUE(pool.addTransaction(tx3));
    EXPECT_EQ(pool.size(), 3);
}

TEST_F(MempoolTest, PendingNonceTracksCorrectly) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    EXPECT_EQ(pool.getPendingNonce(aliceAddr), 0);  // State nonce

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    pool.addTransaction(tx1);
    EXPECT_EQ(pool.getPendingNonce(aliceAddr), 1);

    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 100, 1);
    pool.addTransaction(tx2);
    EXPECT_EQ(pool.getPendingNonce(aliceAddr), 2);
}

TEST_F(MempoolTest, PendingBalanceTracksCorrectly) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    EXPECT_EQ(pool.getPendingBalance(aliceAddr), 10000);  // State balance

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 3000, 0);
    pool.addTransaction(tx1);
    EXPECT_EQ(pool.getPendingBalance(aliceAddr), 7000);

    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 2000, 1);
    pool.addTransaction(tx2);
    EXPECT_EQ(pool.getPendingBalance(aliceAddr), 5000);
}

TEST_F(MempoolTest, PendingBalancePreventOverspend) {
    // Alice has 10000. She submits 8000, then tries 8000 again.
    // The second should fail because pending balance is only 2000.
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 8000, 0);
    EXPECT_TRUE(pool.addTransaction(tx1));

    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 8000, 1);
    EXPECT_FALSE(pool.addTransaction(tx2));  // Only 2000 pending balance left
}

TEST_F(MempoolTest, SkippedNonceRejected) {
    // Alice submits nonce 0, then tries to submit nonce 2 (skipping 1)
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    EXPECT_TRUE(pool.addTransaction(tx1));

    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 100, 2);  // Should be 1
    EXPECT_FALSE(pool.addTransaction(tx2));
}

// =============================================================================
// Block Creation Tests
// =============================================================================

TEST_F(MempoolTest, GetTransactionsForBlockReturnsAll) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    Transaction tx2 = createTransaction(bobKeys, aliceAddr, 200, 0);
    pool.addTransaction(tx1);
    pool.addTransaction(tx2);

    auto txs = pool.getTransactionsForBlock(100);
    EXPECT_EQ(txs.size(), 2);
}

TEST_F(MempoolTest, GetTransactionsForBlockRespectsMaxCount) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 100, 1);
    Transaction tx3 = createTransaction(aliceKeys, bobAddr, 100, 2);
    pool.addTransaction(tx1);
    pool.addTransaction(tx2);
    pool.addTransaction(tx3);

    auto txs = pool.getTransactionsForBlock(2);
    EXPECT_EQ(txs.size(), 2);
}

TEST_F(MempoolTest, GetTransactionsForBlockOrdersByNonce) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    // Add transactions in mixed order (they'll be stored by hash, not by nonce)
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 200, 1);
    Transaction tx3 = createTransaction(aliceKeys, bobAddr, 300, 2);
    pool.addTransaction(tx1);
    pool.addTransaction(tx2);
    pool.addTransaction(tx3);

    auto txs = pool.getTransactionsForBlock(100);
    ASSERT_EQ(txs.size(), 3);

    // All should be from Alice, in nonce order
    EXPECT_EQ(txs[0].nonce, 0);
    EXPECT_EQ(txs[1].nonce, 1);
    EXPECT_EQ(txs[2].nonce, 2);
}

TEST_F(MempoolTest, GetTransactionsDoesNotRemoveThem) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
    pool.addTransaction(tx);

    pool.getTransactionsForBlock(100);
    EXPECT_EQ(pool.size(), 1);  // Still in pool
}

TEST_F(MempoolTest, EmptyMempoolReturnsEmptyVector) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    auto txs = pool.getTransactionsForBlock(100);
    EXPECT_EQ(txs.size(), 0);
}

// =============================================================================
// Post-Mining Cleanup Tests
// =============================================================================

TEST_F(MempoolTest, RemoveMinedTransactions) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 200, 1);
    pool.addTransaction(tx1);
    pool.addTransaction(tx2);
    EXPECT_EQ(pool.size(), 2);

    // Mine block with tx1
    Block block = makeBlock(genesis, {tx1});
    pool.removeMinedTransactions(block);

    EXPECT_EQ(pool.size(), 1);
    EXPECT_FALSE(pool.contains(tx1.hash));
    EXPECT_TRUE(pool.contains(tx2.hash));
}

TEST_F(MempoolTest, RemoveMinedIgnoresUnknownTransactions) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    pool.addTransaction(tx1);

    // Mine a block with a transaction NOT in the mempool
    Transaction otherTx = createTransaction(bobKeys, aliceAddr, 50, 0);
    Block block = makeBlock(genesis, {otherTx});
    pool.removeMinedTransactions(block);

    EXPECT_EQ(pool.size(), 1);  // Still has tx1
}

// =============================================================================
// Re-validation Tests
// =============================================================================

TEST_F(MempoolTest, RevalidateKeepsValidTransactions) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
    pool.addTransaction(tx);

    size_t evicted = pool.revalidate();
    EXPECT_EQ(evicted, 0);
    EXPECT_EQ(pool.size(), 1);
}

TEST_F(MempoolTest, RevalidateEvictsInvalidAfterStateChange) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    // Alice submits two pending transactions
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 5000, 0);
    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 4000, 1);
    pool.addTransaction(tx1);
    pool.addTransaction(tx2);
    EXPECT_EQ(pool.size(), 2);

    // Now a DIFFERENT block arrives that debits Alice 7000 ODM
    // (from another node — not from our mempool)
    Transaction externalTx = createTransaction(aliceKeys, bobAddr, 7000, 0);
    Block externalBlock = makeBlock(genesis, {externalTx});
    state.applyBlock(externalBlock);

    // Alice now has 3000 confirmed. Remove mined (tx1 nonce 0 might match)
    pool.removeMinedTransactions(externalBlock);

    // Revalidate: tx1 (5000, nonce 1) exceeds 3000 balance after state change
    // Actually after the external block, Alice has nonce 1 and 3000 ODM
    // tx1 was nonce 0 — which was already used. tx2 was nonce 1.
    // After removeMinedTransactions, externalTx might not match our pool txs.
    // Let's revalidate — both should be evicted because:
    // - tx1 (nonce 0) is now stale (Alice's confirmed nonce is 1)
    // - tx2 (nonce 1, 4000 ODM) — Alice only has 3000 ODM
    size_t evicted = pool.revalidate();
    EXPECT_GT(evicted, 0);
}

TEST_F(MempoolTest, RevalidateRebuildsCorrectPendingState) {
    StateManager state = makeDefaultState();
    Mempool pool(state);

    // Alice submits nonces 0, 1, 2
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 200, 1);
    Transaction tx3 = createTransaction(aliceKeys, bobAddr, 300, 2);
    pool.addTransaction(tx1);
    pool.addTransaction(tx2);
    pool.addTransaction(tx3);

    // Mine nonce 0 in a block
    Block block = makeBlock(genesis, {tx1});
    state.applyBlock(block);
    pool.removeMinedTransactions(block);

    // Revalidate: nonce 1 and 2 should survive
    // (Alice's confirmed nonce is now 1, confirmed balance is 9900)
    size_t evicted = pool.revalidate();
    EXPECT_EQ(evicted, 0);
    EXPECT_EQ(pool.size(), 2);

    // Pending state should reflect the two remaining txs
    EXPECT_EQ(pool.getPendingNonce(aliceAddr), 3);
    EXPECT_EQ(pool.getPendingBalance(aliceAddr), 9400);  // 9900 - 200 - 300
}

// =============================================================================
// Full Lifecycle Test
// =============================================================================

TEST_F(MempoolTest, FullTransactionLifecycle) {
    // This test simulates the complete lifecycle:
    //   1. Submit transactions to mempool
    //   2. Validator pulls them for a block
    //   3. Block is created and mined
    //   4. State is updated
    //   5. Mempool cleans up

    StateManager state = makeDefaultState();
    Mempool pool(state);

    // Step 1: Submit transactions
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 500, 0);
    Transaction tx2 = createTransaction(bobKeys, aliceAddr, 300, 0);
    EXPECT_TRUE(pool.addTransaction(tx1));
    EXPECT_TRUE(pool.addTransaction(tx2));
    EXPECT_EQ(pool.size(), 2);

    // Step 2: Get transactions for block
    auto blockTxs = pool.getTransactionsForBlock(100);
    EXPECT_EQ(blockTxs.size(), 2);

    // Step 3: Create and validate block
    Block block = makeBlock(genesis, blockTxs);

    // Step 4: Apply block to state
    EXPECT_TRUE(state.applyBlock(block));

    // Step 5: Clean up mempool
    pool.removeMinedTransactions(block);
    pool.revalidate();

    EXPECT_EQ(pool.size(), 0);

    // Verify final state
    EXPECT_EQ(state.getBalance(aliceAddr), 9800);   // 10000 - 500 + 300
    EXPECT_EQ(state.getBalance(bobAddr), 10200);     // 10000 + 500 - 300
    EXPECT_EQ(state.getNonce(aliceAddr), 1);
    EXPECT_EQ(state.getNonce(bobAddr), 1);
}
