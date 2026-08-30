// =============================================================================
// TitanCore — PoA Consensus Tests
// =============================================================================
//
// These tests verify the Proof of Authority consensus mechanism:
// round-robin scheduling, authority validation, and integration with
// the Blockchain class.
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/core/consensus.hpp"
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

class ConsensusTest : public ::testing::Test {
protected:
    // Three authorities for testing round-robin
    KeyPair auth0, auth1, auth2;
    Address addr0, addr1, addr2;

    // An outsider (not an authority)
    KeyPair outsider;
    Address outsiderAddr;

    void SetUp() override {
        auth0 = generateKeyPair();
        auth1 = generateKeyPair();
        auth2 = generateKeyPair();
        addr0 = deriveAddress(auth0.publicKey);
        addr1 = deriveAddress(auth1.publicKey);
        addr2 = deriveAddress(auth2.publicKey);

        outsider = generateKeyPair();
        outsiderAddr = deriveAddress(outsider.publicKey);
    }

    // Helper: create consensus with 3 authorities
    PoAConsensus makeConsensus() {
        return PoAConsensus({addr0, addr1, addr2});
    }
};

// =============================================================================
// Construction Tests
// =============================================================================

TEST_F(ConsensusTest, ConstructWithAuthorities) {
    PoAConsensus poa({addr0, addr1, addr2});
    EXPECT_EQ(poa.authorityCount(), 3);
}

TEST_F(ConsensusTest, EmptyAuthoritiesThrows) {
    EXPECT_THROW(PoAConsensus({}), std::invalid_argument);
}

TEST_F(ConsensusTest, SingleAuthority) {
    PoAConsensus poa({addr0});
    EXPECT_EQ(poa.authorityCount(), 1);
}

// =============================================================================
// Round-Robin Scheduling Tests
// =============================================================================

TEST_F(ConsensusTest, RoundRobinRotation) {
    PoAConsensus poa = makeConsensus();

    // Block 0 → auth0, Block 1 → auth1, Block 2 → auth2
    EXPECT_EQ(poa.getProducer(0), addr0);
    EXPECT_EQ(poa.getProducer(1), addr1);
    EXPECT_EQ(poa.getProducer(2), addr2);
}

TEST_F(ConsensusTest, RoundRobinWrapsAround) {
    PoAConsensus poa = makeConsensus();

    // After cycling through all 3, it wraps back to auth0
    EXPECT_EQ(poa.getProducer(3), addr0);
    EXPECT_EQ(poa.getProducer(4), addr1);
    EXPECT_EQ(poa.getProducer(5), addr2);
    EXPECT_EQ(poa.getProducer(6), addr0);
}

TEST_F(ConsensusTest, RoundRobinLargeIndex) {
    PoAConsensus poa = makeConsensus();

    // index 999 % 3 = 0 → auth0
    EXPECT_EQ(poa.getProducer(999), addr0);
    // index 1000 % 3 = 1 → auth1
    EXPECT_EQ(poa.getProducer(1000), addr1);
}

TEST_F(ConsensusTest, SingleAuthorityAlwaysProduces) {
    PoAConsensus poa({addr0});

    // With one authority, every block is theirs
    EXPECT_EQ(poa.getProducer(0), addr0);
    EXPECT_EQ(poa.getProducer(1), addr0);
    EXPECT_EQ(poa.getProducer(100), addr0);
}

// =============================================================================
// Authority Membership Tests
// =============================================================================

TEST_F(ConsensusTest, IsAuthorityTrue) {
    PoAConsensus poa = makeConsensus();

    EXPECT_TRUE(poa.isAuthority(addr0));
    EXPECT_TRUE(poa.isAuthority(addr1));
    EXPECT_TRUE(poa.isAuthority(addr2));
}

TEST_F(ConsensusTest, IsAuthorityFalse) {
    PoAConsensus poa = makeConsensus();

    EXPECT_FALSE(poa.isAuthority(outsiderAddr));
}

TEST_F(ConsensusTest, GetAuthoritiesReturnsAll) {
    PoAConsensus poa = makeConsensus();

    auto auths = poa.getAuthorities();
    EXPECT_EQ(auths.size(), 3);
    EXPECT_EQ(auths[0], addr0);
    EXPECT_EQ(auths[1], addr1);
    EXPECT_EQ(auths[2], addr2);
}

// =============================================================================
// Block Validation Tests
// =============================================================================

TEST_F(ConsensusTest, ValidateBlockCorrectProducer) {
    PoAConsensus poa = makeConsensus();

    // Genesis (index 0) should be produced by auth0
    Block genesis = createGenesisBlock(auth0);
    EXPECT_TRUE(poa.validateBlock(genesis));
}

TEST_F(ConsensusTest, ValidateBlockWrongProducer) {
    PoAConsensus poa = makeConsensus();

    // Genesis (index 0) should be auth0, but we create it with auth1
    Block genesis = createGenesisBlock(auth1);
    EXPECT_FALSE(poa.validateBlock(genesis));
}

TEST_F(ConsensusTest, ValidateBlockWrongProducerSetsError) {
    PoAConsensus poa = makeConsensus();

    Block genesis = createGenesisBlock(auth1);  // Wrong authority for index 0
    poa.validateBlock(genesis);
    EXPECT_FALSE(poa.getLastError().empty());
}

TEST_F(ConsensusTest, ValidateBlockSequence) {
    PoAConsensus poa = makeConsensus();

    // Build a valid sequence: each block by the correct authority
    Block b0 = createGenesisBlock(auth0);
    EXPECT_TRUE(poa.validateBlock(b0));

    Block b1 = createBlock(auth1, b0, {});
    EXPECT_TRUE(poa.validateBlock(b1));

    Block b2 = createBlock(auth2, b1, {});
    EXPECT_TRUE(poa.validateBlock(b2));

    Block b3 = createBlock(auth0, b2, {});  // Wraps around
    EXPECT_TRUE(poa.validateBlock(b3));
}

TEST_F(ConsensusTest, OutsiderBlockRejected) {
    PoAConsensus poa = makeConsensus();

    // Outsider produces genesis — not in authority set
    Block genesis = createGenesisBlock(outsider);
    EXPECT_FALSE(poa.validateBlock(genesis));
}

// =============================================================================
// Blockchain Integration Tests
// =============================================================================

TEST_F(ConsensusTest, BlockchainWithPoAAcceptsCorrectProducer) {
    PoAConsensus poa = makeConsensus();
    Blockchain chain(auth0, &poa);

    // Block 1 should be produced by auth1
    Block b1 = createBlock(auth1, chain.getLatestBlock(), {});
    EXPECT_TRUE(chain.addBlock(b1));
}

TEST_F(ConsensusTest, BlockchainWithPoARejectsWrongProducer) {
    PoAConsensus poa = makeConsensus();
    Blockchain chain(auth0, &poa);

    // Block 1 should be auth1, but auth2 tries to produce it
    Block b1 = createBlock(auth2, chain.getLatestBlock(), {});
    EXPECT_FALSE(chain.addBlock(b1));
}

TEST_F(ConsensusTest, BlockchainWithPoAErrorMessage) {
    PoAConsensus poa = makeConsensus();
    Blockchain chain(auth0, &poa);

    Block b1 = createBlock(auth0, chain.getLatestBlock(), {});  // auth0 again? Wrong.
    chain.addBlock(b1);

    // Error should mention "PoA"
    EXPECT_NE(chain.getLastError().find("PoA"), std::string::npos);
}

TEST_F(ConsensusTest, BlockchainWithPoAFullRotation) {
    PoAConsensus poa = makeConsensus();
    Blockchain chain(auth0, &poa);

    // Build blocks 1-6, each by the correct authority
    KeyPair authorities[] = {auth0, auth1, auth2};
    for (int i = 1; i <= 6; ++i) {
        KeyPair& producer = authorities[i % 3];
        Block block = createBlock(producer, chain.getLatestBlock(), {});
        EXPECT_TRUE(chain.addBlock(block))
            << "Block " << i << " should be accepted (producer: "
            << (i % 3) << ")";
    }

    EXPECT_EQ(chain.getHeight(), 7);  // genesis + 6 blocks
}

TEST_F(ConsensusTest, BlockchainWithoutPoAAcceptsAnyValidator) {
    // Without PoA (old behavior), any valid block is accepted
    Blockchain chain(auth0);

    // auth2 produces block 1 — normally auth1's turn, but no PoA
    Block b1 = createBlock(auth2, chain.getLatestBlock(), {});
    EXPECT_TRUE(chain.addBlock(b1));
}
