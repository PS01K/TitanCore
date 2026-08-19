// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates the transaction module by:
//   1. Creating two "wallets" (key pairs)
//   2. Creating a signed transaction (Alice sends 50 ODM to Bob)
//   3. Displaying the full transaction as JSON
//   4. Verifying the transaction
//   5. Showing that tampering is detected
//
// In future milestones, this will evolve into the actual node process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/core/transaction.hpp"
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

    // ---- Step 1: Create two "wallets" ----

    spdlog::info("");
    spdlog::info("[Transaction Demo] Creating wallets...");

    KeyPair alice = generateKeyPair();
    Address aliceAddr = deriveAddress(alice.publicKey);
    spdlog::info("  Alice's address: {}", toHex(aliceAddr));

    KeyPair bob = generateKeyPair();
    Address bobAddr = deriveAddress(bob.publicKey);
    spdlog::info("  Bob's address:   {}", toHex(bobAddr));

    // ---- Step 2: Alice sends 50 ODM to Bob ----

    spdlog::info("");
    spdlog::info("[Transaction Demo] Alice sends 50 ODM to Bob...");

    Transaction tx = createTransaction(alice, bobAddr, 50, 0);

    spdlog::info("  Transaction Hash (txid): {}", toHex(tx.hash));
    spdlog::info("  Nonce: {}", tx.nonce);
    spdlog::info("  Timestamp: {}", tx.timestamp);

    // ---- Step 3: Display the full transaction JSON ----

    spdlog::info("");
    spdlog::info("[Transaction Demo] Full transaction JSON:");
    nlohmann::json txJson = toJson(tx);
    spdlog::info("{}", txJson.dump(2));  // Pretty-print with 2-space indent

    // ---- Step 4: Verify the transaction ----

    spdlog::info("");
    bool valid = verifyTransaction(tx);
    spdlog::info("[Transaction Demo] Transaction valid: {}", valid ? "YES" : "NO");

    // ---- Step 5: Show tampering detection ----

    spdlog::info("");
    spdlog::info("[Transaction Demo] Tampering with the amount (50 → 5000)...");
    tx.amount = 5000;
    bool tamperedValid = verifyTransaction(tx);
    spdlog::info("[Transaction Demo] Tampered transaction valid: {}",
                 tamperedValid ? "YES" : "NO");

    spdlog::info("");
    spdlog::info("Transaction module operational. Ready for Milestone 3: Blocks.");

    return 0;
}
