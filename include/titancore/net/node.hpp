#pragma once

// =============================================================================
// TitanCore — Node (Orchestrator)
// =============================================================================
//
// The Node class ties all components together into a running blockchain
// node. It owns the blockchain, state manager, consensus engine, and
// network manager, and coordinates between them.
//
// This is where TitanCore becomes a DISTRIBUTED SYSTEM:
//   - When a block is produced locally, it's broadcast to all peers
//   - When a block arrives from a peer, it's validated and applied
//   - When a new peer connects, chain synchronization begins
//
// THREAD SAFETY:
//   The blockchain, state, and mempool are accessed from both the main
//   thread (produceBlock, queries) and the io_context thread (message
//   handlers). A mutex protects all shared state.
//
//   The pattern is:
//     1. Lock mutex
//     2. Access shared state (blockchain, state)
//     3. Unlock mutex
//     4. Broadcast (doesn't need the lock — NetworkManager has its own)
// =============================================================================

#include "titancore/core/block.hpp"
#include "titancore/core/blockchain.hpp"
#include "titancore/core/consensus.hpp"
#include "titancore/core/state.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/net/network_manager.hpp"
#include "titancore/net/message.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace titancore {
namespace net {

class Node {
public:
    // Create a node with all necessary configuration.
    //
    // Parameters:
    //   nodeKeys           — this node's key pair (for signing blocks)
    //   genesisValidatorKeys — authority 0's keys (same for all nodes,
    //                          so all produce the same genesis block)
    //   authorities        — the ordered list of authority addresses
    //   genesisAllocations — initial account balances at genesis
    //   listenPort         — TCP port to listen for incoming peers
    Node(const crypto::KeyPair& nodeKeys,
         const crypto::KeyPair& genesisValidatorKeys,
         std::vector<Address> authorities,
         core::AddressMap<uint64_t> genesisAllocations,
         uint16_t listenPort);

    ~Node();

    // Non-copyable
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    // Start networking (launches the io_context thread).
    void start();

    // Stop networking and clean up.
    void stop();

    // Connect to a peer at the given host:port.
    void connectToPeer(const std::string& host, uint16_t port);

    // =========================================================================
    // Block Production
    // =========================================================================

    // Attempt to produce a block.
    // Returns true if this node is the designated producer for the next
    // block index and the block was successfully created and broadcast.
    bool produceBlock();

    // =========================================================================
    // Queries (thread-safe)
    // =========================================================================

    uint64_t getChainHeight() const;
    bool isChainValid() const;
    const Address& getAddress() const;
    uint16_t getListenPort() const;
    size_t getPeerCount() const;

private:
    // =========================================================================
    // Message Handling
    // =========================================================================

    void handleMessage(const Message& msg, PeerSession::Ptr from);
    void onNewBlock(const nlohmann::json& payload, PeerSession::Ptr from);
    void onGetBlocks(const nlohmann::json& payload, PeerSession::Ptr from);
    void onBlocks(const nlohmann::json& payload);
    void onPeerConnected(PeerSession::Ptr peer);

    // =========================================================================
    // Node Identity
    // =========================================================================

    crypto::KeyPair nodeKeys_;
    Address nodeAddr_;

    // =========================================================================
    // Core Components (protected by mutex_)
    // =========================================================================

    mutable std::mutex mutex_;
    std::unique_ptr<core::PoAConsensus> consensus_;
    std::unique_ptr<core::Blockchain> blockchain_;
    std::unique_ptr<core::StateManager> state_;

    // Genesis allocations (needed for genesis allocation setup)
    core::AddressMap<uint64_t> genesisAllocations_;

    // =========================================================================
    // Networking
    // =========================================================================

    std::unique_ptr<NetworkManager> network_;
};

} // namespace net
} // namespace titancore
