// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates the blockchain module by:
//   1. Initializing a blockchain with a genesis block
//   2. Adding blocks with transactions
//   3. Validating the entire chain
//   4. Looking up a transaction by hash
//   5. Showing that invalid blocks are rejected
//
// In future milestones, this will evolve into the actual node process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/blockchain.hpp"
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
    spdlog::info("[Blockchain Demo] Creating participants...");

    KeyPair validator = generateKeyPair();
    spdlog::info("  Validator: {}", toHex(deriveAddress(validator.publicKey)));

    KeyPair alice = generateKeyPair();
    spdlog::info("  Alice:     {}", toHex(deriveAddress(alice.publicKey)));

    KeyPair bob = generateKeyPair();
    Address bobAddr = deriveAddress(bob.publicKey);
    spdlog::info("  Bob:       {}", toHex(bobAddr));

    // ---- Step 2: Initialize the blockchain ----

    spdlog::info("");
    Blockchain chain(validator);
    spdlog::info("  Chain height: {}", chain.getHeight());

    // ---- Step 3: Add blocks with transactions ----

    spdlog::info("");
    spdlog::info("[Blockchain Demo] Adding blocks with transactions...");

    // Block 1: Alice sends 50 ODM to Bob
    Transaction tx1 = createTransaction(alice, bobAddr, 50, 0);
    Block block1 = createBlock(validator, chain.getLatestBlock(), {tx1});
    bool added1 = chain.addBlock(block1);
    spdlog::info("  Block 1 added: {} (height: {})", added1 ? "YES" : "NO", chain.getHeight());

    // Block 2: Alice sends 30 ODM to Bob
    Transaction tx2 = createTransaction(alice, bobAddr, 30, 1);
    Block block2 = createBlock(validator, chain.getLatestBlock(), {tx2});
    bool added2 = chain.addBlock(block2);
    spdlog::info("  Block 2 added: {} (height: {})", added2 ? "YES" : "NO", chain.getHeight());

    // Block 3: Alice sends 20 ODM to Bob
    Transaction tx3 = createTransaction(alice, bobAddr, 20, 2);
    Block block3 = createBlock(validator, chain.getLatestBlock(), {tx3});
    bool added3 = chain.addBlock(block3);
    spdlog::info("  Block 3 added: {} (height: {})", added3 ? "YES" : "NO", chain.getHeight());

    // ---- Step 4: Validate the entire chain ----

    spdlog::info("");
    bool valid = chain.validateChain();
    spdlog::info("[Blockchain Demo] Full chain validation: {}", valid ? "PASSED" : "FAILED");

    // ---- Step 5: Look up a transaction ----

    spdlog::info("");
    spdlog::info("[Blockchain Demo] Looking up tx1 by hash...");
    const Transaction* found = chain.findTransaction(tx1.hash);
    if (found) {
        spdlog::info("  Found! Amount: {} ODM, Nonce: {}", found->amount, found->nonce);
    } else {
        spdlog::info("  Not found!");
    }

    // ---- Step 6: Try to add an invalid block ----

    spdlog::info("");
    spdlog::info("[Blockchain Demo] Attempting to add an invalid block...");

    // Create a block that links to genesis instead of the latest block
    Block badBlock = createBlock(validator, chain.getBlock(0), {});
    bool addedBad = chain.addBlock(badBlock);
    spdlog::info("  Invalid block accepted: {}", addedBad ? "YES" : "NO");
    if (!addedBad) {
        spdlog::info("  Reason: {}", chain.getLastError());
    }

    spdlog::info("");
    spdlog::info("Blockchain operational. Height: {}, Chain valid: {}",
                 chain.getHeight(), chain.validateChain() ? "YES" : "NO");

    return 0;
}
