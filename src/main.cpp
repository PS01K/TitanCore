// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates the block module by:
//   1. Creating a genesis block (Block 0)
//   2. Creating a transaction (Alice sends 50 ODM to Bob)
//   3. Creating Block 1 containing the transaction, linked to genesis
//   4. Verifying the chain
//   5. Showing that tampering is detected
//
// In future milestones, this will evolve into the actual node process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/core/block.hpp"
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
    spdlog::info("[Block Demo] Creating participants...");

    KeyPair validator = generateKeyPair();
    Address validatorAddr = deriveAddress(validator.publicKey);
    spdlog::info("  Validator: {}", toHex(validatorAddr));

    KeyPair alice = generateKeyPair();
    Address aliceAddr = deriveAddress(alice.publicKey);
    spdlog::info("  Alice:     {}", toHex(aliceAddr));

    KeyPair bob = generateKeyPair();
    Address bobAddr = deriveAddress(bob.publicKey);
    spdlog::info("  Bob:       {}", toHex(bobAddr));

    // ---- Step 2: Create the genesis block ----

    spdlog::info("");
    spdlog::info("[Block Demo] Creating genesis block (Block 0)...");

    Block genesis = createGenesisBlock(validator);

    spdlog::info("  Index:        {}", genesis.index);
    spdlog::info("  Timestamp:    {}", genesis.timestamp);
    spdlog::info("  Previous:     {}", toHex(genesis.previousHash));
    spdlog::info("  Block Hash:   {}", toHex(genesis.hash));
    spdlog::info("  Transactions: {}", genesis.transactions.size());
    spdlog::info("  Valid:        {}", verifyBlock(genesis) ? "YES" : "NO");

    // ---- Step 3: Create a transaction and put it in Block 1 ----

    spdlog::info("");
    spdlog::info("[Block Demo] Alice sends 50 ODM to Bob...");

    Transaction tx = createTransaction(alice, bobAddr, 50, 0);
    spdlog::info("  Tx Hash: {}", toHex(tx.hash));

    spdlog::info("");
    spdlog::info("[Block Demo] Creating Block 1 (linked to genesis)...");

    Block block1 = createBlock(validator, genesis, {tx});

    spdlog::info("  Index:        {}", block1.index);
    spdlog::info("  Previous:     {}", toHex(block1.previousHash));
    spdlog::info("  Block Hash:   {}", toHex(block1.hash));
    spdlog::info("  Transactions: {}", block1.transactions.size());
    spdlog::info("  Valid:        {}", verifyBlock(block1) ? "YES" : "NO");

    // ---- Step 4: Verify the chain linkage ----

    spdlog::info("");
    bool chainLinked = (block1.previousHash == genesis.hash);
    spdlog::info("[Block Demo] Chain linked: Block 1 → Genesis: {}",
                 chainLinked ? "YES" : "NO");

    // ---- Step 5: Show tampering detection ----

    spdlog::info("");
    spdlog::info("[Block Demo] Tampering: modifying transaction amount in Block 1...");
    block1.transactions[0].amount = 5000;
    spdlog::info("[Block Demo] Block 1 still valid: {}",
                 verifyBlock(block1) ? "YES" : "NO");

    spdlog::info("");
    spdlog::info("Block module operational. Ready for Milestone 4: Blockchain.");

    return 0;
}
