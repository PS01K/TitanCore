// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// Demonstrates the full multi-node blockchain workflow:
//
//   1. Three authority nodes start on localhost (ports 9001-9003)
//   2. Nodes connect in a mesh topology
//   3. Real transactions are submitted (ODM transfers between accounts)
//   4. Blocks are produced with transactions from the mempool
//   5. All nodes validate, apply, and stay synchronized
//   6. Balances change correctly across all nodes
//   7. Nodes shut down, then one restarts and recovers from LevelDB
//
// In a real deployment, each node would run as a separate process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/net/node.hpp"
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

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
  using namespace titancore::net;

  // ---- Clean up previous demo data ----
  std::filesystem::remove_all("demo_data");
  std::filesystem::create_directories("demo_data/node0");
  std::filesystem::create_directories("demo_data/node1");
  std::filesystem::create_directories("demo_data/node2");

  // ---- Step 1: Create three authority nodes ----

  spdlog::info("");
  spdlog::info("=== Step 1: Create 3 authority nodes ===");

  KeyPair auth0 = generateKeyPair();
  KeyPair auth1 = generateKeyPair();
  KeyPair auth2 = generateKeyPair();
  Address addr0 = deriveAddress(auth0.publicKey);
  Address addr1 = deriveAddress(auth1.publicKey);
  Address addr2 = deriveAddress(auth2.publicKey);

  spdlog::info("  Authority 0: {}", toHex(addr0));
  spdlog::info("  Authority 1: {}", toHex(addr1));
  spdlog::info("  Authority 2: {}", toHex(addr2));

  std::vector<Address> authorities = {addr0, addr1, addr2};
  AddressMap<uint64_t> allocations;
  allocations[addr0] = 10000;
  allocations[addr1] = 10000;
  allocations[addr2] = 10000;

  // ---- Step 2: Start all nodes with LevelDB persistence ----

  spdlog::info("");
  spdlog::info("=== Step 2: Start nodes (with LevelDB) ===");

  auto node0 = std::make_unique<Node>(auth0, auth0, authorities, allocations,
                                      9001, "demo_data/node0");
  auto node1 = std::make_unique<Node>(auth1, auth0, authorities, allocations,
                                      9002, "demo_data/node1");
  auto node2 = std::make_unique<Node>(auth2, auth0, authorities, allocations,
                                      9003, "demo_data/node2");

  node0->start();
  node1->start();
  node2->start();

  // Connect in mesh: node1→node0, node2→node0
  node1->connectToPeer("127.0.0.1", 9001);
  node2->connectToPeer("127.0.0.1", 9001);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  spdlog::info("  Peers: node0={}, node1={}, node2={}", node0->getPeerCount(),
               node1->getPeerCount(), node2->getPeerCount());

  // ---- Step 3: Show initial balances ----

  spdlog::info("");
  spdlog::info("=== Step 3: Initial balances ===");
  spdlog::info("  Auth0: {} ODM", node0->getBalance(addr0));
  spdlog::info("  Auth1: {} ODM", node0->getBalance(addr1));
  spdlog::info("  Auth2: {} ODM", node0->getBalance(addr2));

  // ---- Step 4: Submit real transactions ----

  spdlog::info("");
  spdlog::info("=== Step 4: Submit transactions ===");

  // auth0 → auth1: 500 ODM
  Transaction tx1 =
      createTransaction(auth0, addr1, 500, node0->getNonce(addr0));
  bool ok = node0->submitTransaction(tx1);
  spdlog::info("  auth0 → auth1: 500 ODM (accepted: {})", ok ? "YES" : "NO");

  // auth1 → auth2: 200 ODM
  Transaction tx2 =
      createTransaction(auth1, addr2, 200, node1->getNonce(addr1));
  ok = node1->submitTransaction(tx2);
  spdlog::info("  auth1 → auth2: 200 ODM (accepted: {})", ok ? "YES" : "NO");

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  spdlog::info("  Mempool: node0={}, node1={}, node2={}",
               node0->getMempoolSize(), node1->getMempoolSize(),
               node2->getMempoolSize());

  // ---- Step 5: Produce blocks ----

  spdlog::info("");
  spdlog::info("=== Step 5: Produce blocks (round-robin) ===");

  // Block 1: auth1's turn (index 1 % 3 = 1) — should include pending txs
  spdlog::info("  --- Block 1 (Authority 1's turn) ---");
  ok = node1->produceBlock();
  spdlog::info("  Produced: {}", ok ? "YES" : "NO");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  spdlog::info("  Heights: node0={}, node1={}, node2={}",
               node0->getChainHeight(), node1->getChainHeight(),
               node2->getChainHeight());

  // Block 2: auth2's turn (index 2 % 3 = 2)
  spdlog::info("  --- Block 2 (Authority 2's turn) ---");
  ok = node2->produceBlock();
  spdlog::info("  Produced: {}", ok ? "YES" : "NO");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  spdlog::info("  Heights: node0={}, node1={}, node2={}",
               node0->getChainHeight(), node1->getChainHeight(),
               node2->getChainHeight());

  // ---- Step 6: Verify balances across all nodes ----

  spdlog::info("");
  spdlog::info("=== Step 6: Verify balances ===");
  spdlog::info("  Expected: auth0=9500, auth1=10300, auth2=10200");
  spdlog::info("");
  spdlog::info("  Node 0 view:");
  spdlog::info("    auth0={}, auth1={}, auth2={}", node0->getBalance(addr0),
               node0->getBalance(addr1), node0->getBalance(addr2));
  spdlog::info("  Node 1 view:");
  spdlog::info("    auth0={}, auth1={}, auth2={}", node1->getBalance(addr0),
               node1->getBalance(addr1), node1->getBalance(addr2));
  spdlog::info("  Node 2 view:");
  spdlog::info("    auth0={}, auth1={}, auth2={}", node2->getBalance(addr0),
               node2->getBalance(addr1), node2->getBalance(addr2));

  // Verify chain validity
  spdlog::info("");
  spdlog::info("  Chain valid: node0={}, node1={}, node2={}",
               node0->isChainValid() ? "YES" : "NO",
               node1->isChainValid() ? "YES" : "NO",
               node2->isChainValid() ? "YES" : "NO");

  // ---- Step 7: Destroy all nodes (releases LevelDB locks) ----

  spdlog::info("");
  spdlog::info("=== Step 7: Shut down all nodes ===");
  node0.reset();
  node1.reset();
  node2.reset();
  spdlog::info("  All nodes destroyed (LevelDB locks released).");

  // ---- Step 8: Restart node0 from LevelDB ----

  spdlog::info("");
  spdlog::info("=== Step 8: Restart node0 from LevelDB ===");

  // Create a new Node instance pointing at the same data directory.
  // It should recover the chain and state from disk.
  auto node0_restarted = std::make_unique<Node>(
      auth0, auth0, authorities, allocations, 9010, "demo_data/node0");
  node0_restarted->start();

  spdlog::info("  Recovered chain height: {}",
               node0_restarted->getChainHeight());
  spdlog::info("  Recovered balances:");
  spdlog::info(
      "    auth0={}, auth1={}, auth2={}", node0_restarted->getBalance(addr0),
      node0_restarted->getBalance(addr1), node0_restarted->getBalance(addr2));

  node0_restarted.reset();

  // ---- Summary ----

  spdlog::info("");
  spdlog::info("=============================================");
  spdlog::info("  Multi-node demo complete.");
  spdlog::info("  3 nodes, real transactions, LevelDB persistence.");
  spdlog::info("=============================================");

  // Clean up
  std::filesystem::remove_all("demo_data");

  return 0;
}
