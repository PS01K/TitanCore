// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// Three modes of operation:
//
//   1. BOOTSTRAP: Generate keys and genesis config for a new network
//      ./titancore_node --generate-keys --count 3 --genesis-dir genesis/
//
//   2. NODE: Run a single node (for multi-machine deployment)
//      ./titancore_node --index 0 --port 9001 --peers 192.168.1.5:9002 \
//                       --data-dir data/node0 --genesis-dir genesis/
//
//   3. DEMO: Run the 3-node-in-one-process demo (no args)
//      ./titancore_node
//
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/key_io.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/net/genesis_config.hpp"
#include "titancore/net/node.hpp"
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Global flag for graceful shutdown via Ctrl+C
static std::atomic<bool> g_running{true};

static void signalHandler(int /*signum*/) { g_running = false; }

// =============================================================================
// CLI Argument Parsing
// =============================================================================

struct CliArgs {
  // Mode flags
  bool generateKeys = false;
  bool showHelp = false;

  // Bootstrap mode
  int count = 3;
  std::string genesisDir = "genesis/";

  // Node mode
  int index = -1; // -1 = not set (run demo mode)
  uint16_t port = 9000;
  std::string dataDir = "";
  std::vector<std::pair<std::string, uint16_t>> peers;
};

static CliArgs parseArgs(int argc, char *argv[]) {
  CliArgs args;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      args.showHelp = true;
    } else if (arg == "--generate-keys") {
      args.generateKeys = true;
    } else if (arg == "--count" && i + 1 < argc) {
      args.count = std::stoi(argv[++i]);
    } else if (arg == "--genesis-dir" && i + 1 < argc) {
      args.genesisDir = argv[++i];
    } else if (arg == "--index" && i + 1 < argc) {
      args.index = std::stoi(argv[++i]);
    } else if (arg == "--port" && i + 1 < argc) {
      args.port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--data-dir" && i + 1 < argc) {
      args.dataDir = argv[++i];
    } else if (arg == "--peers" && i + 1 < argc) {
      // Parse comma-separated host:port pairs
      std::string peersStr = argv[++i];
      std::istringstream stream(peersStr);
      std::string token;
      while (std::getline(stream, token, ',')) {
        auto colonPos = token.rfind(':');
        if (colonPos != std::string::npos) {
          std::string host = token.substr(0, colonPos);
          uint16_t peerPort =
              static_cast<uint16_t>(std::stoi(token.substr(colonPos + 1)));
          args.peers.push_back({host, peerPort});
        }
      }
    } else {
      spdlog::warn("Unknown argument: {}", arg);
    }
  }

  return args;
}

static void printUsage() {
  std::cout
      << "TitanCore Node v" << titancore::VERSION_STRING << "\n\n"
      << "Usage:\n"
      << "  titancore_node                           Run 3-node demo "
         "(localhost)\n"
      << "  titancore_node --generate-keys [options] Generate keys & genesis "
         "config\n"
      << "  titancore_node --index N [options]       Run a single node\n\n"
      << "Bootstrap options:\n"
      << "  --count N          Number of authority key pairs (default: 3)\n"
      << "  --genesis-dir DIR  Output directory (default: genesis/)\n\n"
      << "Node options:\n"
      << "  --index N          This node's authority index (0, 1, 2, ...)\n"
      << "  --port P           Listen port (default: 9000)\n"
      << "  --peers H:P,...    Comma-separated peer addresses\n"
      << "  --data-dir DIR     LevelDB data directory\n"
      << "  --genesis-dir DIR  Genesis config directory (default: "
         "genesis/)\n\n";
}

// =============================================================================
// Mode 1: Generate Keys
// =============================================================================

static int runGenerateKeys(const CliArgs &args) {
  using namespace titancore;
  using namespace titancore::crypto;
  using namespace titancore::net;

  spdlog::info("Generating {} authority key pairs...", args.count);

  // Create the genesis directory
  std::filesystem::create_directories(args.genesisDir);

  GenesisConfig config;

  for (int i = 0; i < args.count; ++i) {
    KeyPair kp = generateKeyPair();
    Address addr = deriveAddress(kp.publicKey);

    AuthorityInfo auth;
    auth.address = addr;
    auth.publicKey = kp.publicKey;
    auth.allocation = 10000; // Default 10,000 ODM each

    config.authorities.push_back(auth);

    // Save individual private key file
    std::string keyPath =
        args.genesisDir + "/auth" + std::to_string(i) + ".key";
    savePrivateKey(kp.privateKey, keyPath);
    spdlog::info("  Authority {}: {} → {}", i, toHex(addr), keyPath);

    // Authority 0's private key is the genesis validator key
    if (i == 0) {
      config.genesisValidatorPrivateKey = kp.privateKey;
    }
  }

  // Save the genesis config
  std::string configPath = args.genesisDir + "/genesis.json";
  saveGenesisConfig(config, configPath);
  spdlog::info("");
  spdlog::info("Genesis config written to: {}", configPath);
  spdlog::info("Key files written to: {}/auth*.key", args.genesisDir);
  spdlog::info("");
  spdlog::info("Next steps:");
  spdlog::info("  1. Copy genesis/ to all machines");
  spdlog::info("  2. Each machine keeps only its own auth<N>.key");
  spdlog::info(
      "  3. Run: ./titancore_node --index <N> --port <P> --peers <H:P,...>");

  return 0;
}

// =============================================================================
// Mode 2: Run Single Node
// =============================================================================

static int runNode(const CliArgs &args) {
  using namespace titancore;
  using namespace titancore::crypto;
  using namespace titancore::net;

  // Load genesis config
  std::string configPath = args.genesisDir + "/genesis.json";
  spdlog::info("Loading genesis config from: {}", configPath);
  GenesisConfig genesis = loadGenesisConfig(configPath);

  if (args.index < 0 ||
      args.index >= static_cast<int>(genesis.authorities.size())) {
    spdlog::error("Invalid index {}. Genesis has {} authorities.", args.index,
                  genesis.authorities.size());
    return 1;
  }

  // Load this node's private key
  std::string keyPath =
      args.genesisDir + "/auth" + std::to_string(args.index) + ".key";
  spdlog::info("Loading key from: {}", keyPath);
  PrivateKey privKey = loadPrivateKey(keyPath);
  KeyPair nodeKeys;
  nodeKeys.privateKey = privKey;
  nodeKeys.publicKey = derivePublicKey(privKey);

  // Verify the loaded key matches the expected authority
  Address nodeAddr = deriveAddress(nodeKeys.publicKey);
  if (nodeAddr != genesis.authorities[args.index].address) {
    spdlog::error("Key mismatch! Loaded key produces address {}, "
                  "but genesis expects {} for authority {}.",
                  toHex(nodeAddr),
                  toHex(genesis.authorities[args.index].address), args.index);
    return 1;
  }

  // Set up data directory
  std::string dataDir = args.dataDir;
  if (dataDir.empty()) {
    dataDir = "data/node" + std::to_string(args.index);
  }
  std::filesystem::create_directories(dataDir);

  // Create the node
  auto node =
      std::make_unique<Node>(nodeKeys, genesis.getGenesisValidatorKeys(),
                             genesis.getAuthorityAddresses(),
                             genesis.getAllocations(), args.port, dataDir);

  node->start();

  // Connect to configured peers
  for (const auto &[host, port] : args.peers) {
    spdlog::info("[Main] Connecting to peer {}:{}...", host, port);
    node->connectToPeer(host, port);
  }

  // Install signal handler for graceful shutdown
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  spdlog::info("");
  spdlog::info("=============================================");
  spdlog::info("  Node {} running on port {}", args.index, args.port);
  spdlog::info("  Press Ctrl+C to shut down");
  spdlog::info("=============================================");
  spdlog::info("");

  // Main loop: attempt to produce blocks periodically
  while (g_running) {
    node->produceBlock();
    // Sleep 2 seconds between block production attempts
    for (int i = 0; i < 20 && g_running; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  spdlog::info("");
  spdlog::info("Shutting down...");
  node.reset();
  spdlog::info("Node stopped.");

  return 0;
}

// =============================================================================
// Mode 3: Legacy Demo (3 nodes in one process)
// =============================================================================

static int runDemo() {
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

  spdlog::info("  --- Block 1 (Authority 1's turn) ---");
  ok = node1->produceBlock();
  spdlog::info("  Produced: {}", ok ? "YES" : "NO");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  spdlog::info("  Heights: node0={}, node1={}, node2={}",
               node0->getChainHeight(), node1->getChainHeight(),
               node2->getChainHeight());

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

  spdlog::info("");
  spdlog::info("  Chain valid: node0={}, node1={}, node2={}",
               node0->isChainValid() ? "YES" : "NO",
               node1->isChainValid() ? "YES" : "NO",
               node2->isChainValid() ? "YES" : "NO");

  // ---- Step 7: Destroy all nodes ----

  spdlog::info("");
  spdlog::info("=== Step 7: Shut down all nodes ===");
  node0.reset();
  node1.reset();
  node2.reset();
  spdlog::info("  All nodes destroyed (LevelDB locks released).");

  // ---- Step 8: Restart node0 from LevelDB ----

  spdlog::info("");
  spdlog::info("=== Step 8: Restart node0 from LevelDB ===");

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

  return 0;
}

// =============================================================================
// Entry Point
// =============================================================================

int main(int argc, char *argv[]) {
  spdlog::set_level(spdlog::level::info);

  spdlog::info("=============================================");
  spdlog::info("  {} v{}", titancore::PROJECT_NAME, titancore::VERSION_STRING);
  spdlog::info("  Native Currency: {} ({})", titancore::CURRENCY_NAME,
               titancore::CURRENCY_SYMBOL);
  spdlog::info("=============================================");

  CliArgs args = parseArgs(argc, argv);

  if (args.showHelp) {
    printUsage();
    return 0;
  }

  if (args.generateKeys) {
    return runGenerateKeys(args);
  }

  if (args.index >= 0) {
    return runNode(args);
  }

  // No mode flags — run legacy demo
  return runDemo();
}
