#pragma once

// =============================================================================
// TitanCore — Node (Orchestrator)
// =============================================================================
//
// The Node class ties all components together into a running blockchain
// node. It owns the blockchain, state manager, consensus engine, mempool,
// persistent storage, and network manager, and coordinates between them.
//
// This is where TitanCore becomes a DISTRIBUTED SYSTEM:
//   - When a transaction is submitted, it enters the mempool and is
//     broadcast to peers
//   - When a block is produced, transactions are pulled from the mempool
//   - When a block is accepted, state is updated and persisted to LevelDB
//   - When a new peer connects, chain synchronization begins
//   - On restart, chain and state are recovered from LevelDB
//
// THREAD SAFETY:
//   The blockchain, state, mempool, and storage are accessed from both the
//   main thread (produceBlock, submitTransaction, queries) and the
//   io_context thread (message handlers). A mutex protects all shared state.
//
//   The pattern is:
//     1. Lock mutex
//     2. Access shared state (blockchain, state, mempool, storage)
//     3. Unlock mutex
//     4. Broadcast (doesn't need the lock — NetworkManager has its own)
// =============================================================================

#include "titancore/core/block.hpp"
#include "titancore/core/blockchain.hpp"
#include "titancore/core/consensus.hpp"
#include "titancore/core/mempool.hpp"
#include "titancore/core/state.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/net/network_manager.hpp"
#include "titancore/net/message.hpp"
#include "titancore/storage/storage.hpp"

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
    //   dataDir            — LevelDB directory for persistence (empty = in-memory only)
    Node(const crypto::KeyPair& nodeKeys,
         const crypto::KeyPair& genesisValidatorKeys,
         std::vector<Address> authorities,
         core::AddressMap<uint64_t> genesisAllocations,
         uint16_t listenPort,
         const std::string& dataDir = "");

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
    // Transaction Submission
    // =========================================================================

    // Submit a signed transaction to this node.
    // The transaction is validated and added to the mempool. If accepted,
    // it's broadcast to all connected peers via NEW_TX.
    // Returns true if accepted, false if rejected (call getLastError).
    bool submitTransaction(const core::Transaction& tx);

    // =========================================================================
    // Block Production
    // =========================================================================

    // Attempt to produce a block.
    // Pulls pending transactions from the mempool. Returns true if this
    // node is the designated producer and the block was created successfully.
    bool produceBlock();

    // =========================================================================
    // Queries (thread-safe)
    // =========================================================================

    uint64_t getChainHeight() const;
    bool isChainValid() const;
    const Address& getAddress() const;
    uint16_t getListenPort() const;
    size_t getPeerCount() const;
    size_t getMempoolSize() const;
    uint64_t getBalance(const Address& addr) const;
    uint64_t getNonce(const Address& addr) const;
    std::string getLastError() const;

private:
    // =========================================================================
    // Message Handling
    // =========================================================================

    void handleMessage(const Message& msg, PeerSession::Ptr from);
    void onNewBlock(const nlohmann::json& payload, PeerSession::Ptr from);
    void onNewTx(const nlohmann::json& payload, PeerSession::Ptr from);
    void onGetBlocks(const nlohmann::json& payload, PeerSession::Ptr from);
    void onBlocks(const nlohmann::json& payload);
    void onPeerConnected(PeerSession::Ptr peer);

    // =========================================================================
    // Internal Helpers
    // =========================================================================

    // Called after a block is accepted (locally produced or from peer).
    // Removes mined txs from mempool, revalidates, persists to LevelDB.
    void afterBlockAccepted(const core::Block& block);

    // Recover chain and state from LevelDB on startup.
    void recoverFromStorage();

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
    std::unique_ptr<core::Mempool> mempool_;

    // Genesis allocations (needed for genesis allocation setup)
    core::AddressMap<uint64_t> genesisAllocations_;

    // =========================================================================
    // Persistence (optional — null if no dataDir)
    // =========================================================================

    std::unique_ptr<storage::Storage> storage_;

    // =========================================================================
    // Networking
    // =========================================================================

    std::unique_ptr<NetworkManager> network_;

    // Last error message (for submitTransaction failures)
    std::string lastError_;
};

} // namespace net
} // namespace titancore
