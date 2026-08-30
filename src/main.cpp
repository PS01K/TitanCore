// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates Proof of Authority consensus:
//   1. Three authorities are registered
//   2. Each takes turns producing blocks (round-robin)
//   3. An out-of-turn block is rejected
//
// In future milestones, this will evolve into the actual node process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/blockchain.hpp"
#include "titancore/core/consensus.hpp"
#include "titancore/core/state.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include <spdlog/spdlog.h>

int main() {
    spdlog::set_level(spdlog::level::info);

    spdlog::info("=============================================");
    spdlog::info("  {} v{}", titancore::PROJECT_NAME, titancore::VERSION_STRING);
    spdlog::info("  Native Currency: {} ({})",
                 titancore::CURRENCY_NAME, titancore::CURRENCY_SYMBOL);
    spdlog::info("=============================================");

    using namespace titancore;
    using namespace titancore::crypto;
    using namespace titancore::core;

    // ---- Step 1: Create three authority nodes ----

    spdlog::info("");
    spdlog::info("[PoA Demo] Creating authority nodes...");

    KeyPair auth0 = generateKeyPair();
    KeyPair auth1 = generateKeyPair();
    KeyPair auth2 = generateKeyPair();
    Address addr0 = deriveAddress(auth0.publicKey);
    Address addr1 = deriveAddress(auth1.publicKey);
    Address addr2 = deriveAddress(auth2.publicKey);

    spdlog::info("  Authority 0: {}", toHex(addr0));
    spdlog::info("  Authority 1: {}", toHex(addr1));
    spdlog::info("  Authority 2: {}", toHex(addr2));

    // ---- Step 2: Initialize PoA consensus + blockchain ----

    spdlog::info("");
    PoAConsensus poa({addr0, addr1, addr2});
    Blockchain chain(auth0, &poa);  // Genesis by auth0

    AddressMap<uint64_t> allocations;
    allocations[addr0] = 10000;
    allocations[addr1] = 10000;
    allocations[addr2] = 10000;
    StateManager state(allocations);

    spdlog::info("  Chain initialized with PoA (3 authorities, round-robin)");

    // ---- Step 3: Authorities take turns producing blocks ----

    spdlog::info("");
    spdlog::info("[PoA Demo] Round-robin block production...");

    KeyPair* authorities[] = {&auth0, &auth1, &auth2};

    // Produce blocks 1-6 (two full rotations)
    for (int i = 1; i <= 6; ++i) {
        int turn = i % 3;
        KeyPair& producer = *authorities[turn];
        Address producerAddr = deriveAddress(producer.publicKey);

        Block block = createBlock(producer, chain.getLatestBlock(), {});
        bool accepted = chain.addBlock(block);

        spdlog::info("  Block {}: Authority {} → {}",
                     i, turn, accepted ? "ACCEPTED" : "REJECTED");
    }

    spdlog::info("  Chain height: {}", chain.getHeight());

    // ---- Step 4: Wrong authority tries to produce — rejected ----

    spdlog::info("");
    spdlog::info("[PoA Demo] Authority 0 tries to produce block 7 "
                 "(should be Authority 1's turn)...");

    Block wrongBlock = createBlock(auth0, chain.getLatestBlock(), {});
    bool accepted = chain.addBlock(wrongBlock);

    spdlog::info("  Accepted: {}", accepted ? "YES" : "NO");
    if (!accepted) {
        spdlog::info("  Reason: {}", chain.getLastError());
    }

    // ---- Step 5: Correct authority produces block 7 ----

    spdlog::info("");
    spdlog::info("[PoA Demo] Authority 1 produces block 7 (their turn)...");

    Block correctBlock = createBlock(auth1, chain.getLatestBlock(), {});
    accepted = chain.addBlock(correctBlock);

    spdlog::info("  Accepted: {}", accepted ? "YES" : "NO");

    // ---- Summary ----

    spdlog::info("");
    spdlog::info("PoA consensus operational.");
    spdlog::info("  Chain height: {} | Chain valid: {}",
                 chain.getHeight(),
                 chain.validateChain() ? "YES" : "NO");

    return 0;
}

