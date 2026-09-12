// =============================================================================
// TitanCore — Node Integration Tests
// =============================================================================
//
// Tests for the full networking stack including mempool integration,
// transaction propagation, persistence, and recovery.
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/net/node.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/core/transaction.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace titancore;
using namespace titancore::crypto;
using namespace titancore::core;
using namespace titancore::net;

class NodeTest : public ::testing::Test {
protected:
    KeyPair auth0, auth1;
    Address addr0, addr1;
    std::vector<Address> authorities;
    AddressMap<uint64_t> allocations;

    void SetUp() override {
        auth0 = generateKeyPair();
        auth1 = generateKeyPair();
        addr0 = deriveAddress(auth0.publicKey);
        addr1 = deriveAddress(auth1.publicKey);

        authorities = {addr0, addr1};

        allocations[addr0] = 10000;
        allocations[addr1] = 10000;
    }

    void wait(int ms = 500) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
};

// =============================================================================
// Basic Lifecycle
// =============================================================================

TEST_F(NodeTest, CreateAndStartNode) {
    // Port 0 = OS assigns an ephemeral port
    Node node(auth0, auth0, authorities, allocations, 0);
    node.start();

    EXPECT_EQ(node.getChainHeight(), 1);  // Genesis
    EXPECT_EQ(node.getAddress(), addr0);
    EXPECT_GT(node.getListenPort(), 0);

    node.stop();
}

TEST_F(NodeTest, ProduceBlockOnOwnTurn) {
    Node node(auth0, auth0, {addr0}, allocations, 0);
    node.start();

    // With single authority, every block is ours
    EXPECT_TRUE(node.produceBlock());
    EXPECT_EQ(node.getChainHeight(), 2);

    node.stop();
}

TEST_F(NodeTest, CannotProduceOnWrongTurn) {
    Node node(auth1, auth0, authorities, allocations, 0);
    node.start();

    // Block 1 is auth1's turn (index 1 % 2 = 1)
    EXPECT_TRUE(node.produceBlock());

    // Block 2 is auth0's turn — auth1 (this node) can't produce
    EXPECT_FALSE(node.produceBlock());

    node.stop();
}

// =============================================================================
// Two-Node Communication
// =============================================================================

TEST_F(NodeTest, TwoNodesConnect) {
    Node node0(auth0, auth0, authorities, allocations, 0);
    Node node1(auth1, auth0, authorities, allocations, 0);
    node0.start();
    node1.start();

    node1.connectToPeer("127.0.0.1", node0.getListenPort());
    wait(500);

    EXPECT_GE(node0.getPeerCount(), 1);
    EXPECT_GE(node1.getPeerCount(), 1);

    node0.stop();
    node1.stop();
}

TEST_F(NodeTest, BlockPropagation) {
    Node node0(auth0, auth0, authorities, allocations, 0);
    Node node1(auth1, auth0, authorities, allocations, 0);
    node0.start();
    node1.start();

    node1.connectToPeer("127.0.0.1", node0.getListenPort());
    wait(500);

    // Block 1: auth1's turn (index 1 % 2 = 1)
    EXPECT_TRUE(node1.produceBlock());
    wait(500);

    // Both nodes should have height 2
    EXPECT_EQ(node0.getChainHeight(), 2);
    EXPECT_EQ(node1.getChainHeight(), 2);

    node0.stop();
    node1.stop();
}

TEST_F(NodeTest, MultipleBlockPropagation) {
    Node node0(auth0, auth0, authorities, allocations, 0);
    Node node1(auth1, auth0, authorities, allocations, 0);
    node0.start();
    node1.start();

    node1.connectToPeer("127.0.0.1", node0.getListenPort());
    wait(500);

    // Block 1: auth1
    EXPECT_TRUE(node1.produceBlock());
    wait(300);

    // Block 2: auth0
    EXPECT_TRUE(node0.produceBlock());
    wait(300);

    // Block 3: auth1
    EXPECT_TRUE(node1.produceBlock());
    wait(300);

    // Block 4: auth0
    EXPECT_TRUE(node0.produceBlock());
    wait(300);

    // Both nodes should have height 5
    EXPECT_EQ(node0.getChainHeight(), 5);
    EXPECT_EQ(node1.getChainHeight(), 5);

    // Both chains should be valid
    EXPECT_TRUE(node0.isChainValid());
    EXPECT_TRUE(node1.isChainValid());

    node0.stop();
    node1.stop();
}

TEST_F(NodeTest, ChainSyncOnConnect) {
    // Node 0 produces blocks BEFORE node 1 connects
    Node node0(auth0, auth0, {addr0}, allocations, 0);
    node0.start();

    node0.produceBlock();  // Block 1
    node0.produceBlock();  // Block 2
    node0.produceBlock();  // Block 3
    EXPECT_EQ(node0.getChainHeight(), 4);

    // Node 1 connects — should sync and catch up
    Node node1(auth1, auth0, {addr0}, allocations, 0);
    node1.start();

    node1.connectToPeer("127.0.0.1", node0.getListenPort());
    wait(1000);

    // Node 1 should have synced to height 4
    EXPECT_EQ(node1.getChainHeight(), 4);

    node0.stop();
    node1.stop();
}

// =============================================================================
// Transaction Submission & Mempool
// =============================================================================

TEST_F(NodeTest, SubmitTransaction) {
    Node node(auth0, auth0, {addr0}, allocations, 0);
    node.start();

    // Create and submit a transaction
    Transaction tx = createTransaction(auth0, addr1, 500, 0);
    EXPECT_TRUE(node.submitTransaction(tx));
    EXPECT_EQ(node.getMempoolSize(), 1);

    node.stop();
}

TEST_F(NodeTest, SubmitInvalidTransaction) {
    Node node(auth0, auth0, {addr0}, allocations, 0);
    node.start();

    // Try to send more than balance
    Transaction tx = createTransaction(auth0, addr1, 999999, 0);
    EXPECT_FALSE(node.submitTransaction(tx));
    EXPECT_EQ(node.getMempoolSize(), 0);

    node.stop();
}

TEST_F(NodeTest, ProduceBlockWithTransactions) {
    Node node(auth0, auth0, {addr0}, allocations, 0);
    node.start();

    // Submit a transaction
    Transaction tx = createTransaction(auth0, addr1, 500, 0);
    EXPECT_TRUE(node.submitTransaction(tx));
    EXPECT_EQ(node.getMempoolSize(), 1);

    // Produce a block — it should include the transaction
    EXPECT_TRUE(node.produceBlock());

    // Mempool should be empty after mining
    EXPECT_EQ(node.getMempoolSize(), 0);

    // Balances should have changed
    EXPECT_EQ(node.getBalance(addr0), 9500);
    EXPECT_EQ(node.getBalance(addr1), 10500);

    node.stop();
}

// =============================================================================
// Transaction Propagation Over Network
// =============================================================================

TEST_F(NodeTest, TransactionPropagation) {
    Node node0(auth0, auth0, authorities, allocations, 0);
    Node node1(auth1, auth0, authorities, allocations, 0);
    node0.start();
    node1.start();

    node1.connectToPeer("127.0.0.1", node0.getListenPort());
    wait(500);

    // Submit tx on node0
    Transaction tx = createTransaction(auth0, addr1, 500, 0);
    EXPECT_TRUE(node0.submitTransaction(tx));
    wait(300);

    // Node1 should have received the tx via NEW_TX broadcast
    EXPECT_EQ(node1.getMempoolSize(), 1);

    node0.stop();
    node1.stop();
}

TEST_F(NodeTest, BlockWithTxPropagation) {
    Node node0(auth0, auth0, authorities, allocations, 0);
    Node node1(auth1, auth0, authorities, allocations, 0);
    node0.start();
    node1.start();

    node1.connectToPeer("127.0.0.1", node0.getListenPort());
    wait(500);

    // Submit tx on node0
    Transaction tx = createTransaction(auth0, addr1, 500, 0);
    EXPECT_TRUE(node0.submitTransaction(tx));
    wait(300);

    // Block 1: auth1 produces (should include the tx from mempool)
    EXPECT_TRUE(node1.produceBlock());
    wait(500);

    // Both nodes should have height 2
    EXPECT_EQ(node0.getChainHeight(), 2);
    EXPECT_EQ(node1.getChainHeight(), 2);

    // Both nodes should have updated balances
    EXPECT_EQ(node0.getBalance(addr0), 9500);
    EXPECT_EQ(node0.getBalance(addr1), 10500);
    EXPECT_EQ(node1.getBalance(addr0), 9500);
    EXPECT_EQ(node1.getBalance(addr1), 10500);

    // Mempools should be empty
    EXPECT_EQ(node0.getMempoolSize(), 0);
    EXPECT_EQ(node1.getMempoolSize(), 0);

    node0.stop();
    node1.stop();
}

// =============================================================================
// Persistence & Recovery
// =============================================================================

TEST_F(NodeTest, PersistenceAndRecovery) {
    std::string dataDir = "test_data_recovery";
    std::filesystem::remove_all(dataDir);

    {
        // Create a node with persistence, produce blocks with txs
        Node node(auth0, auth0, {addr0}, allocations, 0, dataDir);
        node.start();

        Transaction tx = createTransaction(auth0, addr1, 500, 0);
        EXPECT_TRUE(node.submitTransaction(tx));
        EXPECT_TRUE(node.produceBlock());

        EXPECT_EQ(node.getChainHeight(), 2);
        EXPECT_EQ(node.getBalance(addr0), 9500);
        EXPECT_EQ(node.getBalance(addr1), 10500);

        node.stop();
    }
    // Node destroyed — data persisted in LevelDB

    {
        // Create a NEW node with the same data directory
        Node recovered(auth0, auth0, {addr0}, allocations, 0, dataDir);
        recovered.start();

        // Chain should be recovered
        EXPECT_EQ(recovered.getChainHeight(), 2);

        // Balances should be recovered (from replaying blocks)
        EXPECT_EQ(recovered.getBalance(addr0), 9500);
        EXPECT_EQ(recovered.getBalance(addr1), 10500);

        recovered.stop();
    }

    std::filesystem::remove_all(dataDir);
}

// =============================================================================
// Balance Queries
// =============================================================================

TEST_F(NodeTest, InitialBalances) {
    Node node(auth0, auth0, {addr0}, allocations, 0);
    node.start();

    EXPECT_EQ(node.getBalance(addr0), 10000);
    EXPECT_EQ(node.getBalance(addr1), 10000);
    EXPECT_EQ(node.getNonce(addr0), 0);

    node.stop();
}

TEST_F(NodeTest, BalanceUnknownAddress) {
    Node node(auth0, auth0, {addr0}, allocations, 0);
    node.start();

    // Random address that doesn't exist
    Address unknown = {};
    EXPECT_EQ(node.getBalance(unknown), 0);

    node.stop();
}
