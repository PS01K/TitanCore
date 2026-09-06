// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates P2P networking:
//   1. Two nodes start on localhost (different ports)
//   2. They connect and establish a peer session
//   3. Blocks are produced in round-robin and propagated
//   4. Both nodes end up with the same chain
//
// In a real deployment, each node would run as a separate process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/net/node.hpp"
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

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
    using namespace titancore::net;

    // ---- Step 1: Create two authority nodes ----

    spdlog::info("");
    spdlog::info("[P2P Demo] Creating two authority nodes...");

    KeyPair auth0 = generateKeyPair();
    KeyPair auth1 = generateKeyPair();
    Address addr0 = deriveAddress(auth0.publicKey);
    Address addr1 = deriveAddress(auth1.publicKey);

    spdlog::info("  Authority 0: {}", toHex(addr0));
    spdlog::info("  Authority 1: {}", toHex(addr1));

    std::vector<Address> authorities = {addr0, addr1};
    AddressMap<uint64_t> allocations;
    allocations[addr0] = 10000;
    allocations[addr1] = 10000;

    // ---- Step 2: Start both nodes ----

    spdlog::info("");
    spdlog::info("[P2P Demo] Starting nodes...");

    Node node0(auth0, auth0, authorities, allocations, 9001);
    Node node1(auth1, auth0, authorities, allocations, 9002);

    node0.start();
    node1.start();

    // ---- Step 3: Connect node1 to node0 ----

    spdlog::info("");
    spdlog::info("[P2P Demo] Connecting Node 1 → Node 0...");

    node1.connectToPeer("127.0.0.1", 9001);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    spdlog::info("  Node 0 peers: {}", node0.getPeerCount());
    spdlog::info("  Node 1 peers: {}", node1.getPeerCount());

    // ---- Step 4: Produce blocks in round-robin ----

    spdlog::info("");
    spdlog::info("[P2P Demo] Producing blocks (round-robin)...");

    // Block 1: auth1's turn (index 1 % 2 = 1)
    spdlog::info("  --- Block 1 (Authority 1's turn) ---");
    bool ok = node1.produceBlock();
    spdlog::info("  Node 1 produced: {}", ok ? "YES" : "NO");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    spdlog::info("  Node 0 height: {} | Node 1 height: {}",
                 node0.getChainHeight(), node1.getChainHeight());

    // Block 2: auth0's turn (index 2 % 2 = 0)
    spdlog::info("  --- Block 2 (Authority 0's turn) ---");
    ok = node0.produceBlock();
    spdlog::info("  Node 0 produced: {}", ok ? "YES" : "NO");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    spdlog::info("  Node 0 height: {} | Node 1 height: {}",
                 node0.getChainHeight(), node1.getChainHeight());

    // Block 3: auth1's turn
    spdlog::info("  --- Block 3 (Authority 1's turn) ---");
    ok = node1.produceBlock();
    spdlog::info("  Node 1 produced: {}", ok ? "YES" : "NO");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    spdlog::info("  Node 0 height: {} | Node 1 height: {}",
                 node0.getChainHeight(), node1.getChainHeight());

    // Block 4: auth0's turn
    spdlog::info("  --- Block 4 (Authority 0's turn) ---");
    ok = node0.produceBlock();
    spdlog::info("  Node 0 produced: {}", ok ? "YES" : "NO");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    spdlog::info("  Node 0 height: {} | Node 1 height: {}",
                 node0.getChainHeight(), node1.getChainHeight());

    // ---- Summary ----

    spdlog::info("");
    spdlog::info("P2P networking operational.");
    spdlog::info("  Node 0: height={}, valid={}, peers={}",
                 node0.getChainHeight(),
                 node0.isChainValid() ? "YES" : "NO",
                 node0.getPeerCount());
    spdlog::info("  Node 1: height={}, valid={}, peers={}",
                 node1.getChainHeight(),
                 node1.isChainValid() ? "YES" : "NO",
                 node1.getPeerCount());

    // ---- Cleanup ----

    spdlog::info("");
    spdlog::info("[P2P Demo] Shutting down...");
    node0.stop();
    node1.stop();
    spdlog::info("Done.");

    return 0;
}
