// =============================================================================
// TitanCore — Node Integration Tests
// =============================================================================
//
// These tests verify the full networking stack: two nodes connecting,
// exchanging blocks, and synchronizing chains.
//
// TESTING ASYNC BEHAVIOR:
//   We use std::this_thread::sleep_for() to wait for async operations
//   to complete. This is not elegant but reliable for V1 tests.
//   Production tests would use condition variables or ASIO timers.
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/net/node.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/crypto/hash.hpp"

#include <chrono>
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
