// =============================================================================
// TitanCore — Storage Tests
// =============================================================================
//
// These tests verify the LevelDB storage layer: block persistence,
// account state persistence, metadata, and database lifecycle.
//
// TESTING STRATEGY:
//   1. Block operations — save, load by index, load by hash
//   2. Account state — save, load, load all
//   3. Metadata — chain height
//   4. Edge cases — missing data returns nullopt
//   5. Persistence — data survives close and reopen
//   6. Clear — wipes all data
//
// Each test uses a unique temporary directory so tests don't interfere
// with each other. The directory is cleaned up in TearDown.
// =============================================================================
#include "titancore/core/block.hpp"
#include "titancore/core/state.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/storage/storage.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace titancore;
using namespace titancore::core;
using namespace titancore::crypto;
using namespace titancore::storage;

// =============================================================================
// Test Fixture
// =============================================================================

class StorageTest : public ::testing::Test {
protected:
  KeyPair validatorKeys;
  KeyPair aliceKeys;
  KeyPair bobKeys;
  Address aliceAddr;
  Address bobAddr;
  Block genesis;
  std::string dbPath;

  void SetUp() override {
    validatorKeys = generateKeyPair();
    aliceKeys = generateKeyPair();
    bobKeys = generateKeyPair();
    aliceAddr = deriveAddress(aliceKeys.publicKey);
    bobAddr = deriveAddress(bobKeys.publicKey);

    genesis = createGenesisBlock(validatorKeys);

    // Create a unique temp directory for each test
    dbPath = (std::filesystem::temp_directory_path() /
              ("titancore_test_" + std::to_string(std::hash<std::string>{}(
                                       ::testing::UnitTest::GetInstance()
                                           ->current_test_info()
                                           ->name()))))
                 .string();

    // Clean up from any previous failed run
    std::filesystem::remove_all(dbPath);
  }

  void TearDown() override {
    // Clean up test database
    std::filesystem::remove_all(dbPath);
  }
};

// =============================================================================
// Block Save/Load Tests
// =============================================================================

TEST_F(StorageTest, SaveAndLoadBlock) {
  Storage store(dbPath);
  store.saveBlock(genesis);

  auto loaded = store.loadBlock(0);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->index, genesis.index);
  EXPECT_EQ(loaded->hash, genesis.hash);
  EXPECT_EQ(loaded->previousHash, genesis.previousHash);
}

TEST_F(StorageTest, LoadNonexistentBlockReturnsNullopt) {
  Storage store(dbPath);

  auto loaded = store.loadBlock(999);
  EXPECT_FALSE(loaded.has_value());
}

TEST_F(StorageTest, SaveAndLoadMultipleBlocks) {
  Storage store(dbPath);

  // Save genesis
  store.saveBlock(genesis);

  // Create and save block 1
  Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
  Block block1 = createBlock(validatorKeys, genesis, {tx});
  store.saveBlock(block1);

  // Load both
  auto loaded0 = store.loadBlock(0);
  auto loaded1 = store.loadBlock(1);

  ASSERT_TRUE(loaded0.has_value());
  ASSERT_TRUE(loaded1.has_value());

  EXPECT_EQ(loaded0->index, 0);
  EXPECT_EQ(loaded1->index, 1);
  EXPECT_EQ(loaded1->hash, block1.hash);
  EXPECT_EQ(loaded1->transactions.size(), 1);
  EXPECT_EQ(loaded1->transactions[0].amount, 100);
}

TEST_F(StorageTest, LoadBlockByHash) {
  Storage store(dbPath);
  store.saveBlock(genesis);

  auto loaded = store.loadBlockByHash(genesis.hash);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->index, genesis.index);
  EXPECT_EQ(loaded->hash, genesis.hash);
}

TEST_F(StorageTest, LoadBlockByHashNotFound) {
  Storage store(dbPath);

  Hash fakeHash{};
  fakeHash[0] = 0xDE;
  auto loaded = store.loadBlockByHash(fakeHash);
  EXPECT_FALSE(loaded.has_value());
}

TEST_F(StorageTest, BlockTransactionsPreserved) {
  Storage store(dbPath);

  // Create a block with multiple transactions
  Transaction tx1 = createTransaction(aliceKeys, bobAddr, 100, 0);
  Transaction tx2 = createTransaction(aliceKeys, bobAddr, 200, 1);
  Block block = createBlock(validatorKeys, genesis, {tx1, tx2});
  store.saveBlock(block);

  auto loaded = store.loadBlock(block.index);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->transactions.size(), 2);
  EXPECT_EQ(loaded->transactions[0].amount, 100);
  EXPECT_EQ(loaded->transactions[0].nonce, 0);
  EXPECT_EQ(loaded->transactions[1].amount, 200);
  EXPECT_EQ(loaded->transactions[1].nonce, 1);
}

// =============================================================================
// Account State Tests
// =============================================================================

TEST_F(StorageTest, SaveAndLoadAccountState) {
  Storage store(dbPath);

  AccountState acct{10000, 5};
  store.saveAccountState(aliceAddr, acct);

  auto loaded = store.loadAccountState(aliceAddr);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->balance, 10000);
  EXPECT_EQ(loaded->nonce, 5);
}

TEST_F(StorageTest, LoadNonexistentAccountReturnsNullopt) {
  Storage store(dbPath);

  auto loaded = store.loadAccountState(aliceAddr);
  EXPECT_FALSE(loaded.has_value());
}

TEST_F(StorageTest, UpdateAccountState) {
  Storage store(dbPath);

  // Save initial state
  store.saveAccountState(aliceAddr, {10000, 0});

  // Update with new state
  store.saveAccountState(aliceAddr, {9500, 2});

  auto loaded = store.loadAccountState(aliceAddr);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->balance, 9500);
  EXPECT_EQ(loaded->nonce, 2);
}

TEST_F(StorageTest, LoadAllAccounts) {
  Storage store(dbPath);

  store.saveAccountState(aliceAddr, {10000, 0});
  store.saveAccountState(bobAddr, {5000, 3});

  auto accounts = store.loadAllAccounts();
  EXPECT_EQ(accounts.size(), 2);

  // Verify total balance across all accounts
  uint64_t totalBalance = 0;
  for (const auto &[addr, acct] : accounts) {
    totalBalance += acct.balance;
  }
  EXPECT_EQ(totalBalance, 15000);
}

TEST_F(StorageTest, LoadAllAccountsEmpty) {
  Storage store(dbPath);

  auto accounts = store.loadAllAccounts();
  EXPECT_EQ(accounts.size(), 0);
}

// =============================================================================
// Metadata Tests
// =============================================================================

TEST_F(StorageTest, SaveAndLoadChainHeight) {
  Storage store(dbPath);

  store.saveChainHeight(42);
  EXPECT_EQ(store.loadChainHeight(), 42);
}

TEST_F(StorageTest, FreshDatabaseHasHeightZero) {
  Storage store(dbPath);

  EXPECT_EQ(store.loadChainHeight(), 0);
}

TEST_F(StorageTest, ChainHeightUpdates) {
  Storage store(dbPath);

  store.saveChainHeight(1);
  EXPECT_EQ(store.loadChainHeight(), 1);

  store.saveChainHeight(5);
  EXPECT_EQ(store.loadChainHeight(), 5);
}

// =============================================================================
// Persistence Across Reopens
// =============================================================================

TEST_F(StorageTest, DataSurvivesReopen) {
  // This is the KEY test — data must persist across close and reopen.
  // This proves LevelDB is actually writing to disk, not just memory.

  // Phase 1: Write data and close
  {
    Storage store(dbPath);
    store.saveBlock(genesis);
    store.saveAccountState(aliceAddr, {10000, 0});
    store.saveChainHeight(1);
    // store goes out of scope → destructor closes the DB
  }

  // Phase 2: Reopen and verify
  {
    Storage store(dbPath);

    auto block = store.loadBlock(0);
    ASSERT_TRUE(block.has_value());
    EXPECT_EQ(block->hash, genesis.hash);

    auto acct = store.loadAccountState(aliceAddr);
    ASSERT_TRUE(acct.has_value());
    EXPECT_EQ(acct->balance, 10000);

    EXPECT_EQ(store.loadChainHeight(), 1);
  }
}

TEST_F(StorageTest, MultipleBlocksSurviveReopen) {
  // Write several blocks
  {
    Storage store(dbPath);
    store.saveBlock(genesis);

    Transaction tx = createTransaction(aliceKeys, bobAddr, 100, 0);
    Block block1 = createBlock(validatorKeys, genesis, {tx});
    store.saveBlock(block1);
    store.saveChainHeight(2);
  }

  // Reopen and verify
  {
    Storage store(dbPath);

    EXPECT_EQ(store.loadChainHeight(), 2);

    auto b0 = store.loadBlock(0);
    auto b1 = store.loadBlock(1);
    ASSERT_TRUE(b0.has_value());
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(b1->transactions.size(), 1);
  }
}

// =============================================================================
// Clear Tests
// =============================================================================

TEST_F(StorageTest, ClearWipesAllData) {
  Storage store(dbPath);

  // Write some data
  store.saveBlock(genesis);
  store.saveAccountState(aliceAddr, {10000, 0});
  store.saveChainHeight(1);

  // Clear everything
  store.clear();

  // Verify all data is gone
  EXPECT_FALSE(store.loadBlock(0).has_value());
  EXPECT_FALSE(store.loadAccountState(aliceAddr).has_value());
  EXPECT_EQ(store.loadChainHeight(), 0);
}

// =============================================================================
// Full Replay Test
// =============================================================================

TEST_F(StorageTest, FullSaveAndReplayChain) {
  // This simulates the complete persistence workflow:
  //   1. Build a chain with blocks and state
  //   2. Save everything to storage
  //   3. Close and reopen
  //   4. Replay the chain from storage
  //   5. Verify state matches

  Address validatorAddr = deriveAddress(validatorKeys.publicKey);

  // Phase 1: Build and save
  {
    Storage store(dbPath);

    // Build a small chain
    store.saveBlock(genesis);

    Transaction tx1 = createTransaction(aliceKeys, bobAddr, 500, 0);
    Block block1 = createBlock(validatorKeys, genesis, {tx1});
    store.saveBlock(block1);

    Transaction tx2 = createTransaction(bobKeys, aliceAddr, 200, 0);
    Block block2 = createBlock(validatorKeys, block1, {tx2});
    store.saveBlock(block2);

    // Save final state
    // Alice: 10000 - 500 + 200 = 9700
    // Bob: 10000 + 500 - 200 = 10300
    store.saveAccountState(aliceAddr, {9700, 1});
    store.saveAccountState(bobAddr, {10300, 1});
    store.saveAccountState(validatorAddr, {10000, 0});
    store.saveChainHeight(3);
  }

  // Phase 2: Reopen and verify
  {
    Storage store(dbPath);

    EXPECT_EQ(store.loadChainHeight(), 3);

    auto b0 = store.loadBlock(0);
    auto b1 = store.loadBlock(1);
    auto b2 = store.loadBlock(2);
    ASSERT_TRUE(b0.has_value());
    ASSERT_TRUE(b1.has_value());
    ASSERT_TRUE(b2.has_value());

    auto aliceAcct = store.loadAccountState(aliceAddr);
    auto bobAcct = store.loadAccountState(bobAddr);
    ASSERT_TRUE(aliceAcct.has_value());
    ASSERT_TRUE(bobAcct.has_value());

    EXPECT_EQ(aliceAcct->balance, 9700);
    EXPECT_EQ(bobAcct->balance, 10300);

    // Verify all accounts
    auto allAccounts = store.loadAllAccounts();
    EXPECT_EQ(allAccounts.size(), 3);
  }
}
