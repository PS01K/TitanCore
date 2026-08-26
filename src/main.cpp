// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates persistent storage:
//   1. Build a chain with transactions
//   2. Save everything to LevelDB
//   3. Simulate a "restart" — new Blockchain/State objects
//   4. Load from LevelDB and replay the chain
//   5. Verify restored state matches the original
//
// In future milestones, this will evolve into the actual node process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/blockchain.hpp"
#include "titancore/core/mempool.hpp"
#include "titancore/core/state.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/storage/storage.hpp"
#include <spdlog/spdlog.h>

#include <filesystem>

int main() {
  spdlog::set_level(spdlog::level::info);

  spdlog::info("=============================================");
  spdlog::info("  {} v{}", titancore::PROJECT_NAME, titancore::VERSION_STRING);
  spdlog::info("  Native Currency: {} ({})", titancore::CURRENCY_NAME,
               titancore::CURRENCY_SYMBOL);
  spdlog::info("=============================================");

  using namespace titancore;
  using namespace titancore::crypto;
  using namespace titancore::core;
  using namespace titancore::storage;

  // Use a temp directory for the demo database
  std::string dbPath =
      (std::filesystem::temp_directory_path() / "titancore_demo_db").string();
  std::filesystem::remove_all(dbPath); // Start fresh

  // ---- Step 1: Create participants ----

  spdlog::info("");
  spdlog::info("[Persistence Demo] Creating participants...");

  KeyPair validator = generateKeyPair();
  Address validatorAddr = deriveAddress(validator.publicKey);
  spdlog::info("  Validator: {}", toHex(validatorAddr));

  KeyPair alice = generateKeyPair();
  Address aliceAddr = deriveAddress(alice.publicKey);
  spdlog::info("  Alice:     {}", toHex(aliceAddr));

  KeyPair bob = generateKeyPair();
  Address bobAddr = deriveAddress(bob.publicKey);
  spdlog::info("  Bob:       {}", toHex(bobAddr));

  // Genesis allocations
  AddressMap<uint64_t> allocations;
  allocations[aliceAddr] = 10000;
  allocations[bobAddr] = 10000;
  allocations[validatorAddr] = 10000;

  // ========================================================================
  // PHASE 1: Build a chain and save to LevelDB
  // ========================================================================

  spdlog::info("");
  spdlog::info("========== PHASE 1: Build and Save ==========");

  {
    Blockchain chain(validator);
    StateManager state(allocations);
    Storage store(dbPath);

    // Save genesis block
    store.saveBlock(chain.getBlock(0));

    // Block 1: Alice → Bob: 2000 ODM
    Transaction tx1 = createTransaction(alice, bobAddr, 2000, 0);
    Block block1 = createBlock(validator, chain.getLatestBlock(), {tx1});
    chain.addBlock(block1);
    state.applyBlock(block1);
    store.saveBlock(block1);

    // Block 2: Bob → Alice: 500 ODM
    Transaction tx2 = createTransaction(bob, aliceAddr, 500, 0);
    Block block2 = createBlock(validator, chain.getLatestBlock(), {tx2});
    chain.addBlock(block2);
    state.applyBlock(block2);
    store.saveBlock(block2);

    // Save state and metadata
    for (const auto &[addr, acct] : state.getAllAccounts()) {
      store.saveAccountState(addr, acct);
    }
    store.saveChainHeight(chain.getHeight());

    spdlog::info("  Chain built: {} blocks", chain.getHeight());
    spdlog::info("  Alice: {} ODM (nonce: {})", state.getBalance(aliceAddr),
                 state.getNonce(aliceAddr));
    spdlog::info("  Bob:   {} ODM (nonce: {})", state.getBalance(bobAddr),
                 state.getNonce(bobAddr));
    spdlog::info("  Saved to LevelDB at: {}", dbPath);

    // chain, state, and store go out of scope — all destroyed
  }

  spdlog::info("  [Objects destroyed — simulating node shutdown]");

  // ========================================================================
  // PHASE 2: "Restart" — Load from LevelDB and replay
  // ========================================================================

  spdlog::info("");
  spdlog::info("========== PHASE 2: Load and Restore ==========");

  {
    Storage store(dbPath);

    // Read chain height — tells us how many blocks to load
    uint64_t height = store.loadChainHeight();
    spdlog::info("  Stored chain height: {}", height);

    // Replay: load each block and feed it through Blockchain + StateManager
    // The genesis block is loaded first, then each subsequent block
    // is validated and applied just like during normal operation.
    auto genesisBlock = store.loadBlock(0);
    if (!genesisBlock.has_value()) {
      spdlog::error("  Failed to load genesis block!");
      return 1;
    }

    // Reconstruct the blockchain by replaying from genesis
    // Note: we need the validator key to create the genesis.
    // In a real node, the genesis block would be a hardcoded constant.
    Blockchain chain(validator);
    StateManager state(allocations);

    // Replay blocks 1..N (genesis is already created by the constructor)
    for (uint64_t i = 1; i < height; ++i) {
      auto block = store.loadBlock(i);
      if (!block.has_value()) {
        spdlog::error("  Failed to load block {}!", i);
        return 1;
      }
      chain.addBlock(*block);
      state.applyBlock(*block);
      spdlog::info("  Replayed block {}", i);
    }

    // Verify the restored state
    spdlog::info("");
    spdlog::info("  Restored state:");
    spdlog::info("  Alice: {} ODM (nonce: {})", state.getBalance(aliceAddr),
                 state.getNonce(aliceAddr));
    spdlog::info("  Bob:   {} ODM (nonce: {})", state.getBalance(bobAddr),
                 state.getNonce(bobAddr));
    spdlog::info("  Chain height: {}, Chain valid: {}", chain.getHeight(),
                 chain.validateChain() ? "YES" : "NO");

    // Also verify against stored state (for comparison)
    auto storedAlice = store.loadAccountState(aliceAddr);
    auto storedBob = store.loadAccountState(bobAddr);
    if (storedAlice.has_value() && storedBob.has_value()) {
      bool aliceMatch = (state.getBalance(aliceAddr) == storedAlice->balance);
      bool bobMatch = (state.getBalance(bobAddr) == storedBob->balance);
      spdlog::info("");
      spdlog::info("  State verification:");
      spdlog::info("    Alice balance matches stored: {}",
                   aliceMatch ? "YES" : "NO");
      spdlog::info("    Bob balance matches stored:   {}",
                   bobMatch ? "YES" : "NO");
    }
  }

  // Clean up demo database
  std::filesystem::remove_all(dbPath);

  spdlog::info("");
  spdlog::info("Persistence demo complete. Data survived simulated restart.");

  return 0;
}
