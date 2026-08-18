// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates the crypto module by:
//   1. Generating a secp256k1 key pair
//   2. Deriving a blockchain address
//   3. Signing a message hash
//   4. Verifying the signature
//
// In future milestones, this will evolve into the actual node process:
//   - Parse command-line arguments
//   - Initialize storage, load the blockchain
//   - Start P2P networking and RPC server
//   - Enter the main event loop
// =============================================================================

#include "titancore/common/version.hpp"
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

    // ---- Crypto Module Demo ----

    using namespace titancore;
    using namespace titancore::crypto;

    // Step 1: Generate a key pair
    spdlog::info("");
    spdlog::info("[Crypto Demo] Generating secp256k1 key pair...");
    KeyPair kp = generateKeyPair();
    spdlog::info("  Private Key: {}", toHex(kp.privateKey));
    spdlog::info("  Public Key:  {}", toHex(kp.publicKey));

    // Step 2: Derive an address
    Address addr = deriveAddress(kp.publicKey);
    spdlog::info("  Address:     {}", toHex(addr));

    // Step 3: Hash a message and sign it
    std::string message = "Transfer 50 ODM from Alice to Bob";
    Hash msgHash = sha256(message);
    spdlog::info("");
    spdlog::info("[Crypto Demo] Signing message: \"{}\"", message);
    spdlog::info("  Message Hash: {}", toHex(msgHash));

    Signature sig = sign(msgHash, kp.privateKey);
    spdlog::info("  Signature:    {}", toHex(sig));

    // Step 4: Verify the signature
    bool valid = verify(msgHash, sig, kp.publicKey);
    spdlog::info("");
    spdlog::info("[Crypto Demo] Signature valid: {}", valid ? "YES" : "NO");

    // Step 5: Show that tampering breaks verification
    Hash tamperedHash = sha256("Transfer 5000 ODM from Alice to Bob");
    bool tamperedValid = verify(tamperedHash, sig, kp.publicKey);
    spdlog::info("[Crypto Demo] Tampered message valid: {}", tamperedValid ? "YES" : "NO");

    spdlog::info("");
    spdlog::info("Crypto module operational. Ready for Milestone 2: Transactions.");

    return 0;
}
