// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates the state manager by:
//   1. Setting up genesis allocations (10,000 ODM per participant)
//   2. Processing a block with transfers and showing balance changes
//   3. Showing nonce evolution
//   4. Demonstrating that an overspend is rejected
//
// In future milestones, this will evolve into the actual node process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/blockchain.hpp"
#include "titancore/core/state.hpp"
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

    // ---- Step 1: Create participants ----

    spdlog::info("");
    spdlog::info("[State Demo] Creating participants...");

    KeyPair validator = generateKeyPair();
    Address validatorAddr = deriveAddress(validator.publicKey);
    spdlog::info("  Validator: {}", toHex(validatorAddr));

    KeyPair alice = generateKeyPair();
    Address aliceAddr = deriveAddress(alice.publicKey);
    spdlog::info("  Alice:     {}", toHex(aliceAddr));

    KeyPair bob = generateKeyPair();
    Address bobAddr = deriveAddress(bob.publicKey);
    spdlog::info("  Bob:       {}", toHex(bobAddr));

    // ---- Step 2: Initialize blockchain and state ----

    spdlog::info("");
    Blockchain chain(validator);

    // Genesis allocations: everyone gets 10,000 ODM
    AddressMap<uint64_t> allocations;
    allocations[aliceAddr] = 10000;
    allocations[bobAddr] = 10000;
    allocations[validatorAddr] = 10000;
    StateManager state(allocations);

    // Show initial balances
    spdlog::info("");
    spdlog::info("[State Demo] Initial balances:");
    spdlog::info("  Alice:     {} ODM (nonce: {})",
                 state.getBalance(aliceAddr), state.getNonce(aliceAddr));
    spdlog::info("  Bob:       {} ODM (nonce: {})",
                 state.getBalance(bobAddr), state.getNonce(bobAddr));
    spdlog::info("  Validator: {} ODM (nonce: {})",
                 state.getBalance(validatorAddr), state.getNonce(validatorAddr));

    // ---- Step 3: Process a block with transfers ----

    spdlog::info("");
    spdlog::info("[State Demo] Block 1: Alice sends 1500 ODM to Bob...");

    Transaction tx1 = createTransaction(alice, bobAddr, 1500, 0);
    Block block1 = createBlock(validator, chain.getLatestBlock(), {tx1});
    chain.addBlock(block1);
    state.applyBlock(block1);

    spdlog::info("  Alice:     {} ODM (nonce: {})",
                 state.getBalance(aliceAddr), state.getNonce(aliceAddr));
    spdlog::info("  Bob:       {} ODM (nonce: {})",
                 state.getBalance(bobAddr), state.getNonce(bobAddr));

    // ---- Step 4: Second block — Bob sends some back ----

    spdlog::info("");
    spdlog::info("[State Demo] Block 2: Bob sends 500 ODM to Alice...");

    Transaction tx2 = createTransaction(bob, aliceAddr, 500, 0);
    Block block2 = createBlock(validator, chain.getLatestBlock(), {tx2});
    chain.addBlock(block2);
    state.applyBlock(block2);

    spdlog::info("  Alice:     {} ODM (nonce: {})",
                 state.getBalance(aliceAddr), state.getNonce(aliceAddr));
    spdlog::info("  Bob:       {} ODM (nonce: {})",
                 state.getBalance(bobAddr), state.getNonce(bobAddr));

    // ---- Step 5: Try to overspend ----

    spdlog::info("");
    spdlog::info("[State Demo] Block 3: Alice tries to send 99999 ODM (overspend)...");

    Transaction tx3 = createTransaction(alice, bobAddr, 99999, 1);
    Block block3 = createBlock(validator, chain.getLatestBlock(), {tx3});
    chain.addBlock(block3);
    bool applied = state.applyBlock(block3);

    spdlog::info("  Block applied: {}", applied ? "YES" : "NO");
    if (!applied) {
        spdlog::info("  Reason: {}", state.getLastError());
    }

    // Balances should be unchanged
    spdlog::info("  Alice:     {} ODM (unchanged)", state.getBalance(aliceAddr));
    spdlog::info("  Bob:       {} ODM (unchanged)", state.getBalance(bobAddr));

    // ---- Summary ----

    spdlog::info("");
    spdlog::info("State Manager operational. {} accounts tracked.",
                 state.getAllAccounts().size());
    spdlog::info("Chain height: {}, Chain valid: {}",
                 chain.getHeight(), chain.validateChain() ? "YES" : "NO");

    return 0;
}
