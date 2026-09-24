// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// Four modes of operation:
//
//   1. BOOTSTRAP: Generate keys and genesis config for a new network
//      ./titancore_node --generate-keys --count 3 --genesis-dir genesis/
//
//   2. NODE: Run a single node (for multi-machine deployment)
//      ./titancore_node --index 0 --port 9001 --peers 192.168.1.5:9002 \
//                       --data-dir data/node0 --genesis-dir genesis/
//
//   3. SIGN-TX: Create a signed transaction (offline, outputs JSON to stdout)
//      ./titancore_node --sign-tx --key genesis/auth0.key \
//                       --to <address> --amount 500 --nonce 0
//
//   4. HELP: Show usage information
//      ./titancore_node --help
//
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/key_io.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/net/genesis_config.hpp"
#include "titancore/net/node.hpp"
#include "titancore/rpc/rpc_server.hpp"
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
  bool signTx = false;
  bool showHelp = false;

  // Bootstrap mode
  int count = 3;
  std::string genesisDir = "genesis/";

  // Node mode
  int index = -1;
  uint16_t port = 9000;
  uint16_t rpcPort = 8545;
  std::string dataDir = "";
  std::vector<std::pair<std::string, uint16_t>> peers;

  // Sign-tx mode
  std::string keyFile = "";
  std::string toAddress = "";
  uint64_t amount = 0;
  uint64_t nonce = 0;
};

static CliArgs parseArgs(int argc, char *argv[]) {
  CliArgs args;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      args.showHelp = true;
    } else if (arg == "--generate-keys") {
      args.generateKeys = true;
    } else if (arg == "--sign-tx") {
      args.signTx = true;
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
    } else if (arg == "--rpc-port" && i + 1 < argc) {
      args.rpcPort = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--key" && i + 1 < argc) {
      args.keyFile = argv[++i];
    } else if (arg == "--to" && i + 1 < argc) {
      args.toAddress = argv[++i];
    } else if (arg == "--amount" && i + 1 < argc) {
      args.amount = std::stoull(argv[++i]);
    } else if (arg == "--nonce" && i + 1 < argc) {
      args.nonce = std::stoull(argv[++i]);
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
      << "  titancore_node --generate-keys [options] Generate keys & genesis "
         "config\n"
      << "  titancore_node --index N [options]       Run a single node\n"
      << "  titancore_node --sign-tx [options]       Sign a transaction "
         "(offline)\n"
      << "  titancore_node --help                    Show this help\n\n"
      << "Bootstrap options:\n"
      << "  --count N          Number of authority key pairs (default: 3)\n"
      << "  --genesis-dir DIR  Output directory (default: genesis/)\n\n"
      << "Node options:\n"
      << "  --index N          This node's authority index (0, 1, 2, ...)\n"
      << "  --port P           Listen port (default: 9000)\n"
      << "  --peers H:P,...    Comma-separated peer addresses\n"
      << "  --data-dir DIR     LevelDB data directory\n"
      << "  --rpc-port P       JSON-RPC HTTP port (default: 8545)\n"
      << "  --genesis-dir DIR  Genesis config directory (default: "
         "genesis/)\n\n"
      << "Sign-tx options:\n"
      << "  --key FILE         Sender's private key file\n"
      << "  --to ADDRESS       Recipient address (hex)\n"
      << "  --amount N         Amount of ODM to send\n"
      << "  --nonce N          Sender's current nonce\n\n"
      << "Examples:\n"
      << "  # Generate keys for a 3-node network\n"
      << "  titancore_node --generate-keys --count 3\n\n"
      << "  # Run node 0\n"
      << "  titancore_node --index 0 --port 9001 --peers "
         "192.168.1.5:9002\n\n"
      << "  # Sign a transaction offline\n"
      << "  titancore_node --sign-tx --key genesis/auth0.key \\\n"
      << "    --to a1b2c3... --amount 500 --nonce 0\n\n";
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

  // Start JSON-RPC server
  titancore::rpc::RpcServer rpcServer(*node, args.rpcPort);
  rpcServer.start();

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
  spdlog::info("  RPC server on port {}", args.rpcPort);
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
  rpcServer.stop();
  node.reset();
  spdlog::info("Node stopped.");

  return 0;
}

// =============================================================================
// Mode 3: Sign Transaction (offline)
// =============================================================================

static int runSignTx(const CliArgs &args) {
  using namespace titancore;
  using namespace titancore::crypto;
  using namespace titancore::core;

  // Validate required arguments
  if (args.keyFile.empty()) {
    spdlog::error("--sign-tx requires --key <file>");
    return 1;
  }
  if (args.toAddress.empty()) {
    spdlog::error("--sign-tx requires --to <address>");
    return 1;
  }
  if (args.amount == 0) {
    spdlog::error("--sign-tx requires --amount <N> (must be > 0)");
    return 1;
  }

  // Load sender's private key
  PrivateKey privKey = loadPrivateKey(args.keyFile);
  KeyPair senderKeys;
  senderKeys.privateKey = privKey;
  senderKeys.publicKey = derivePublicKey(privKey);

  // Parse recipient address
  Address recipient = fromHexFixed<20>(args.toAddress);

  // Create the signed transaction
  Transaction tx = createTransaction(senderKeys, recipient,
                                     args.amount, args.nonce);

  // Output the signed transaction JSON to stdout
  // (Use cout, not spdlog, so the output is clean JSON for piping)
  std::cout << toJson(tx).dump() << std::endl;

  return 0;
}

// =============================================================================
// Entry Point
// =============================================================================

int main(int argc, char *argv[]) {
  spdlog::set_level(spdlog::level::info);

  // Don't print the banner for --sign-tx (stdout must be clean JSON)
  bool isSigning = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--sign-tx") {
      isSigning = true;
      break;
    }
  }

  if (!isSigning) {
    spdlog::info("=============================================");
    spdlog::info("  {} v{}", titancore::PROJECT_NAME,
                 titancore::VERSION_STRING);
    spdlog::info("  Native Currency: {} ({})", titancore::CURRENCY_NAME,
                 titancore::CURRENCY_SYMBOL);
    spdlog::info("=============================================");
  }

  CliArgs args = parseArgs(argc, argv);

  if (args.showHelp) {
    printUsage();
    return 0;
  }

  if (args.generateKeys) {
    return runGenerateKeys(args);
  }

  if (args.signTx) {
    return runSignTx(args);
  }

  if (args.index >= 0) {
    return runNode(args);
  }

  // No mode flags — show help
  printUsage();
  return 1;
}
