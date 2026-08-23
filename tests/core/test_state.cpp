// =============================================================================
// TitanCore — State Manager Tests
// =============================================================================
//
// These tests verify the State Manager: genesis allocations, balance
// tracking, nonce enforcement, double-spend prevention, atomicity,
// and multi-block state transitions.
//
// TESTING STRATEGY:
//   1. Genesis — accounts created with correct balances
//   2. Transfers — sender debited, recipient credited, nonce incremented
//   3. Nonce enforcement — wrong nonce rejected
//   4. Balance enforcement — overspend rejected
//   5. Atomicity — failed block leaves state unchanged
//   6. Multi-block — state evolves correctly across multiple blocks
//   7. Account creation — recipients auto-created on first receive
// =============================================================================

#include <gtest/gtest.h>
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

class StateTest : public ::testing::Test {
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

    // Helper: create a block with transactions, linked to a previous block
    Block makeBlock(const Block& prev, const std::vector<Transaction>& txs) {
        return createBlock(validatorKeys, prev, txs);
    }
};

// =============================================================================
// Genesis Allocation Tests
// =============================================================================

TEST_F(StateTest, GenesisAllocationsCreateAccounts) {
    StateManager state = makeDefaultState();

    EXPECT_TRUE(state.accountExists(aliceAddr));
    EXPECT_TRUE(state.accountExists(bobAddr));
    EXPECT_FALSE(state.accountExists(carolAddr));  // Not in genesis
}

TEST_F(StateTest, GenesisAllocationsSetCorrectBalances) {
    StateManager state = makeDefaultState();

    EXPECT_EQ(state.getBalance(aliceAddr), 10000);
    EXPECT_EQ(state.getBalance(bobAddr), 10000);
}

TEST_F(StateTest, GenesisAccountsHaveNonceZero) {
    StateManager state = makeDefaultState();

    EXPECT_EQ(state.getNonce(aliceAddr), 0);
    EXPECT_EQ(state.getNonce(bobAddr), 0);
}

TEST_F(StateTest, UnknownAddressHasZeroBalance) {
    StateManager state = makeDefaultState();
    EXPECT_EQ(state.getBalance(carolAddr), 0);
}

TEST_F(StateTest, UnknownAddressHasNonceZero) {
    StateManager state = makeDefaultState();
    EXPECT_EQ(state.getNonce(carolAddr), 0);
}

TEST_F(StateTest, GetAllAccountsReturnsCorrectCount) {
    StateManager state = makeDefaultState();
    EXPECT_EQ(state.getAllAccounts().size(), 2);
}

// =============================================================================
// Successful Transfer Tests
// =============================================================================

TEST_F(StateTest, SimpleTransferDebitsAndCredits) {
    StateManager state = makeDefaultState();

    // Alice sends 100 ODM to Bob
    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
    Block block = makeBlock(genesis, {tx});

    EXPECT_TRUE(state.applyBlock(block));

    EXPECT_EQ(state.getBalance(aliceAddr), 9900);
    EXPECT_EQ(state.getBalance(bobAddr), 10100);
}

TEST_F(StateTest, TransferIncrementsNonce) {
    StateManager state = makeDefaultState();

    Transaction tx = createTransaction(aliceKeys, bobAddr, 50, 0);
    Block block = makeBlock(genesis, {tx});
    state.applyBlock(block);

    EXPECT_EQ(state.getNonce(aliceAddr), 1);
    EXPECT_EQ(state.getNonce(bobAddr), 0);  // Bob didn't send anything
}

TEST_F(StateTest, TransferToNewAccountCreatesIt) {
    StateManager state = makeDefaultState();

    // Carol doesn't exist yet — sending ODM to her creates her account
    EXPECT_FALSE(state.accountExists(carolAddr));

    Transaction tx = createTransaction(aliceKeys, carolAddr, 500, 0);
    Block block = makeBlock(genesis, {tx});
    state.applyBlock(block);

    EXPECT_TRUE(state.accountExists(carolAddr));
    EXPECT_EQ(state.getBalance(carolAddr), 500);
    EXPECT_EQ(state.getNonce(carolAddr), 0);  // New accounts start at nonce 0
}

TEST_F(StateTest, ZeroAmountTransferIsValid) {
    // A zero-amount transfer is valid — it just increments the nonce.
    // Some blockchains use zero-value transactions for on-chain messaging.
    StateManager state = makeDefaultState();

    Transaction tx = createTransaction(aliceKeys, bobAddr, 0, 0);
    Block block = makeBlock(genesis, {tx});

    EXPECT_TRUE(state.applyBlock(block));
    EXPECT_EQ(state.getBalance(aliceAddr), 10000);
    EXPECT_EQ(state.getNonce(aliceAddr), 1);
}

TEST_F(StateTest, SendEntireBalance) {
    StateManager state = makeDefaultState();

    Transaction tx = createTransaction(aliceKeys, bobAddr, 10000, 0);
    Block block = makeBlock(genesis, {tx});

    EXPECT_TRUE(state.applyBlock(block));
    EXPECT_EQ(state.getBalance(aliceAddr), 0);
    EXPECT_EQ(state.getBalance(bobAddr), 20000);
}

// =============================================================================
// Multiple Transactions in One Block
// =============================================================================

TEST_F(StateTest, MultipleTransactionsInOneBlock) {
    StateManager state = makeDefaultState();

    // Alice sends 100 to Bob, then 200 to Carol — in the same block
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
    Transaction tx2 = createTransaction(aliceKeys, carolAddr, 200, 1);
    Block block = makeBlock(genesis, {tx1, tx2});

    EXPECT_TRUE(state.applyBlock(block));

    EXPECT_EQ(state.getBalance(aliceAddr), 9700);   // 10000 - 100 - 200
    EXPECT_EQ(state.getBalance(bobAddr), 10100);
    EXPECT_EQ(state.getBalance(carolAddr), 200);
    EXPECT_EQ(state.getNonce(aliceAddr), 2);
}

TEST_F(StateTest, BothPartiesSendInSameBlock) {
    StateManager state = makeDefaultState();

    // Alice sends to Bob AND Bob sends to Alice in the same block
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 300, 0);
    Transaction tx2 = createTransaction(bobKeys, aliceAddr, 500, 0);
    Block block = makeBlock(genesis, {tx1, tx2});

    EXPECT_TRUE(state.applyBlock(block));

    // Alice: 10000 - 300 + 500 = 10200
    // Bob:   10000 + 300 - 500 = 9800
    EXPECT_EQ(state.getBalance(aliceAddr), 10200);
    EXPECT_EQ(state.getBalance(bobAddr), 9800);
}

// =============================================================================
// Balance Enforcement Tests (Double-Spend Prevention)
// =============================================================================

TEST_F(StateTest, InsufficientBalanceRejected) {
    StateManager state = makeDefaultState();

    // Alice tries to send more than she has
    Transaction tx = createTransaction(aliceKeys, bobAddr, 99999, 0);
    Block block = makeBlock(genesis, {tx});

    EXPECT_FALSE(state.applyBlock(block));
    EXPECT_EQ(state.getBalance(aliceAddr), 10000);  // Unchanged
}

TEST_F(StateTest, DoubleSpendInSameBlockRejected) {
    StateManager state = makeDefaultState();

    // Alice sends 8000 to Bob, then 8000 to Carol — total 16000, but she only has 10000
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 8000, 0);
    Transaction tx2 = createTransaction(aliceKeys, carolAddr, 8000, 1);
    Block block = makeBlock(genesis, {tx1, tx2});

    EXPECT_FALSE(state.applyBlock(block));

    // ATOMICITY: even tx1 was valid on its own, the entire block was rolled back
    EXPECT_EQ(state.getBalance(aliceAddr), 10000);
    EXPECT_EQ(state.getBalance(bobAddr), 10000);
}

TEST_F(StateTest, InsufficientBalanceErrorMessage) {
    StateManager state = makeDefaultState();

    Transaction tx = createTransaction(aliceKeys, bobAddr, 99999, 0);
    Block block = makeBlock(genesis, {tx});
    state.applyBlock(block);

    EXPECT_FALSE(state.getLastError().empty());
}

// =============================================================================
// Nonce Enforcement Tests
// =============================================================================

TEST_F(StateTest, WrongNonceRejected_TooHigh) {
    StateManager state = makeDefaultState();

    // Alice's nonce is 0, but this transaction uses nonce 5
    Transaction tx = createTransaction(aliceKeys, bobAddr, 50, 5);
    Block block = makeBlock(genesis, {tx});

    EXPECT_FALSE(state.applyBlock(block));
    EXPECT_EQ(state.getNonce(aliceAddr), 0);  // Unchanged
}

TEST_F(StateTest, WrongNonceRejected_Replay) {
    StateManager state = makeDefaultState();

    // Process a valid transaction (nonce 0)
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 50, 0);
    Block block1 = makeBlock(genesis, {tx1});
    EXPECT_TRUE(state.applyBlock(block1));
    EXPECT_EQ(state.getNonce(aliceAddr), 1);

    // Try to replay the same nonce (0) — should fail
    Transaction tx2 = createTransaction(aliceKeys, bobAddr, 50, 0);
    Block block2 = makeBlock(block1, {tx2});
    EXPECT_FALSE(state.applyBlock(block2));
    EXPECT_EQ(state.getNonce(aliceAddr), 1);  // Still 1
}

TEST_F(StateTest, SequentialNoncesWork) {
    StateManager state = makeDefaultState();

    // Send three transactions in three blocks with nonces 0, 1, 2
    Block prev = genesis;
    for (uint64_t i = 0; i < 3; ++i) {
        Transaction tx = createTransaction(aliceKeys, bobAddr, 10, i);
        Block block = makeBlock(prev, {tx});
        EXPECT_TRUE(state.applyBlock(block));
        prev = block;
    }

    EXPECT_EQ(state.getNonce(aliceAddr), 3);
    EXPECT_EQ(state.getBalance(aliceAddr), 9970);  // 10000 - 30
}

// =============================================================================
// Sender Account Existence Tests
// =============================================================================

TEST_F(StateTest, NonexistentSenderRejected) {
    StateManager state = makeDefaultState();

    // Carol isn't in genesis — she can't send anything
    Transaction tx = createTransaction(carolKeys, bobAddr, 50, 0);
    Block block = makeBlock(genesis, {tx});

    EXPECT_FALSE(state.applyBlock(block));
}

TEST_F(StateTest, RecipientCanSendAfterReceiving) {
    StateManager state = makeDefaultState();

    // Alice sends 1000 to Carol (creates Carol's account)
    Transaction tx1 = createTransaction(aliceKeys, carolAddr, 1000, 0);
    Block block1 = makeBlock(genesis, {tx1});
    EXPECT_TRUE(state.applyBlock(block1));

    // Now Carol can send to Bob
    Transaction tx2 = createTransaction(carolKeys, bobAddr, 500, 0);
    Block block2 = makeBlock(block1, {tx2});
    EXPECT_TRUE(state.applyBlock(block2));

    EXPECT_EQ(state.getBalance(carolAddr), 500);   // 1000 - 500
    EXPECT_EQ(state.getBalance(bobAddr), 10500);    // 10000 + 500
}

// =============================================================================
// Atomicity Tests
// =============================================================================

TEST_F(StateTest, FailedBlockLeavesStateCompletelyUnchanged) {
    StateManager state = makeDefaultState();

    // Block has 3 transactions: first two are valid, third fails
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);    // Valid
    Transaction tx2 = createTransaction(aliceKeys, carolAddr, 200, 1);  // Valid
    Transaction tx3 = createTransaction(aliceKeys, bobAddr, 99999, 2);  // FAILS: insufficient balance

    Block block = makeBlock(genesis, {tx1, tx2, tx3});
    EXPECT_FALSE(state.applyBlock(block));

    // ALL state should be unchanged — even tx1 and tx2 were rolled back
    EXPECT_EQ(state.getBalance(aliceAddr), 10000);
    EXPECT_EQ(state.getBalance(bobAddr), 10000);
    EXPECT_FALSE(state.accountExists(carolAddr));
    EXPECT_EQ(state.getNonce(aliceAddr), 0);
}

// =============================================================================
// Multi-Block State Evolution
// =============================================================================

TEST_F(StateTest, StateEvolvesAcrossMultipleBlocks) {
    StateManager state = makeDefaultState();

    // Block 1: Alice → Bob: 1000 ODM
    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 1000, 0);
    Block block1 = makeBlock(genesis, {tx1});
    EXPECT_TRUE(state.applyBlock(block1));

    // Block 2: Bob → Carol: 500 ODM
    Transaction tx2 = createTransaction(bobKeys, carolAddr, 500, 0);
    Block block2 = makeBlock(block1, {tx2});
    EXPECT_TRUE(state.applyBlock(block2));

    // Block 3: Carol → Alice: 200 ODM
    Transaction tx3 = createTransaction(carolKeys, aliceAddr, 200, 0);
    Block block3 = makeBlock(block2, {tx3});
    EXPECT_TRUE(state.applyBlock(block3));

    // Final state:
    // Alice: 10000 - 1000 + 200 = 9200
    // Bob:   10000 + 1000 - 500 = 10500
    // Carol: 0 + 500 - 200 = 300
    EXPECT_EQ(state.getBalance(aliceAddr), 9200);
    EXPECT_EQ(state.getBalance(bobAddr), 10500);
    EXPECT_EQ(state.getBalance(carolAddr), 300);

    EXPECT_EQ(state.getNonce(aliceAddr), 1);
    EXPECT_EQ(state.getNonce(bobAddr), 1);
    EXPECT_EQ(state.getNonce(carolAddr), 1);
}

// =============================================================================
// validateTransaction Tests (read-only validation)
// =============================================================================

TEST_F(StateTest, ValidateTransactionDoesNotModifyState) {
    StateManager state = makeDefaultState();

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);

    EXPECT_TRUE(state.validateTransaction(tx));

    // State should be unchanged
    EXPECT_EQ(state.getBalance(aliceAddr), 10000);
    EXPECT_EQ(state.getNonce(aliceAddr), 0);
}

TEST_F(StateTest, ValidateTransactionRejectsInsufficientBalance) {
    StateManager state = makeDefaultState();

    Transaction tx = createTransaction(aliceKeys, bobAddr, 99999, 0);
    EXPECT_FALSE(state.validateTransaction(tx));
}

TEST_F(StateTest, ValidateTransactionRejectsWrongNonce) {
    StateManager state = makeDefaultState();

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 5);
    EXPECT_FALSE(state.validateTransaction(tx));
}

TEST_F(StateTest, ValidateTransactionRejectsUnknownSender) {
    StateManager state = makeDefaultState();

    Transaction tx = createTransaction(carolKeys, bobAddr, 100, 0);
    EXPECT_FALSE(state.validateTransaction(tx));
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(StateTest, EmptyBlockAppliesSuccessfully) {
    StateManager state = makeDefaultState();

    Block block = makeBlock(genesis, {});
    EXPECT_TRUE(state.applyBlock(block));

    // State unchanged
    EXPECT_EQ(state.getBalance(aliceAddr), 10000);
}

TEST_F(StateTest, SelfTransferIsValid) {
    StateManager state = makeDefaultState();

    // Alice sends to herself — balance unchanged, nonce incremented
    Transaction tx = createTransaction(aliceKeys, aliceAddr, 100, 0);
    Block block = makeBlock(genesis, {tx});

    EXPECT_TRUE(state.applyBlock(block));

    EXPECT_EQ(state.getBalance(aliceAddr), 10000);  // Unchanged (debited then credited)
    EXPECT_EQ(state.getNonce(aliceAddr), 1);
}
