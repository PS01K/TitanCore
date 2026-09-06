#include "titancore/net/node.hpp"
#include "titancore/core/block.hpp"
#include "titancore/crypto/hash.hpp"

#include <spdlog/spdlog.h>

namespace titancore {
namespace net {

// =============================================================================
// Construction / Destruction
// =============================================================================

Node::Node(const crypto::KeyPair& nodeKeys,
           const crypto::KeyPair& genesisValidatorKeys,
           std::vector<Address> authorities,
           core::AddressMap<uint64_t> genesisAllocations,
           uint16_t listenPort)
    : nodeKeys_(nodeKeys)
    , nodeAddr_(crypto::deriveAddress(nodeKeys.publicKey))
    , genesisAllocations_(std::move(genesisAllocations))
{
    // Create consensus engine
    consensus_ = std::make_unique<core::PoAConsensus>(std::move(authorities));

    // Create blockchain with PoA enforcement.
    // All nodes use the same genesisValidatorKeys so they produce identical
    // genesis blocks. In production, the genesis would be hardcoded.
    blockchain_ = std::make_unique<core::Blockchain>(
        genesisValidatorKeys, consensus_.get());

    // Create state manager with initial allocations
    state_ = std::make_unique<core::StateManager>(genesisAllocations_);

    // Create network manager
    network_ = std::make_unique<NetworkManager>(listenPort);

    // Wire up callbacks
    network_->setMessageCallback(
        [this](const Message& msg, PeerSession::Ptr from) {
            handleMessage(msg, from);
        });

    network_->setOnPeerConnected(
        [this](PeerSession::Ptr peer) {
            onPeerConnected(peer);
        });

    spdlog::info("[Node] Created: address={}, port={}",
                 crypto::toHex(nodeAddr_), network_->getListenPort());
}

Node::~Node() {
    stop();
}

// =============================================================================
// Lifecycle
// =============================================================================

void Node::start() {
    network_->start();
    spdlog::info("[Node] Started (chain height: {})", blockchain_->getHeight());
}

void Node::stop() {
    if (network_) {
        network_->stop();
    }
}

void Node::connectToPeer(const std::string& host, uint16_t port) {
    network_->connectToPeer(host, port);
}

// =============================================================================
// Block Production
// =============================================================================

bool Node::produceBlock() {
    Message broadcastMsg;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check if it's our turn
        uint64_t nextIndex = blockchain_->getHeight();
        const Address& expected = consensus_->getProducer(nextIndex);
        if (expected != nodeAddr_) {
            spdlog::debug("[Node] Not our turn for block {} (expected: {})",
                          nextIndex, crypto::toHex(expected));
            return false;
        }

        // Create the block (empty for now — mempool integration is future)
        core::Block block = core::createBlock(
            nodeKeys_, blockchain_->getLatestBlock(), {});

        // Add to our chain
        if (!blockchain_->addBlock(block)) {
            spdlog::warn("[Node] Failed to add own block: {}",
                         blockchain_->getLastError());
            return false;
        }

        // Apply to state
        state_->applyBlock(block);

        spdlog::info("[Node] Produced block {}: {}",
                     block.index, crypto::toHex(block.hash));

        // Prepare broadcast message (OUTSIDE the lock scope below)
        broadcastMsg.type = MessageType::NEW_BLOCK;
        broadcastMsg.payload = core::toJson(block);
    }

    // Broadcast to peers (no lock needed — NetworkManager has its own)
    network_->broadcast(broadcastMsg);
    return true;
}

// =============================================================================
// Queries
// =============================================================================

uint64_t Node::getChainHeight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blockchain_->getHeight();
}

bool Node::isChainValid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blockchain_->validateChain();
}

const Address& Node::getAddress() const {
    return nodeAddr_;
}

uint16_t Node::getListenPort() const {
    return network_->getListenPort();
}

size_t Node::getPeerCount() const {
    return network_->peerCount();
}

// =============================================================================
// Message Handling
// =============================================================================

void Node::handleMessage(const Message& msg, PeerSession::Ptr from) {
    switch (msg.type) {
        case MessageType::PING: {
            // Respond with PONG
            Message pong;
            pong.type = MessageType::PONG;
            from->send(pong);
            break;
        }
        case MessageType::PONG:
            // Just acknowledge — no action needed
            break;

        case MessageType::NEW_BLOCK:
            onNewBlock(msg.payload, from);
            break;

        case MessageType::GET_BLOCKS:
            onGetBlocks(msg.payload, from);
            break;

        case MessageType::BLOCKS:
            onBlocks(msg.payload);
            break;

        case MessageType::NEW_TX:
            // Future: add to mempool and broadcast
            spdlog::debug("[Node] Received NEW_TX (not yet implemented)");
            break;
    }
}

void Node::onNewBlock(const nlohmann::json& payload, PeerSession::Ptr from) {
    core::Block block = core::blockFromJson(payload);

    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // If the block is ahead of us, we might need to sync first
        uint64_t ourHeight = blockchain_->getHeight();
        if (block.index > ourHeight) {
            // We're behind — request missing blocks
            spdlog::info("[Node] Block {} is ahead of our height {}, requesting sync",
                         block.index, ourHeight);
            Message getBlocks;
            getBlocks.type = MessageType::GET_BLOCKS;
            getBlocks.payload = {{"from", ourHeight}};
            from->send(getBlocks);
            return;
        }

        if (block.index < ourHeight) {
            // We already have this block — ignore
            spdlog::debug("[Node] Ignoring block {} (already at height {})",
                          block.index, ourHeight);
            return;
        }

        // block.index == ourHeight — this is the next block we need
        if (!blockchain_->addBlock(block)) {
            spdlog::warn("[Node] Rejected block {}: {}",
                         block.index, blockchain_->getLastError());
            return;
        }

        state_->applyBlock(block);
        accepted = true;

        spdlog::info("[Node] Accepted block {} from peer (height: {})",
                     block.index, blockchain_->getHeight());
    }

    // Forward to other peers (outside the lock)
    if (accepted) {
        Message fwd;
        fwd.type = MessageType::NEW_BLOCK;
        fwd.payload = payload;
        network_->broadcast(fwd, from);
    }
}

void Node::onGetBlocks(const nlohmann::json& payload, PeerSession::Ptr from) {
    uint64_t fromIndex = payload.at("from").get<uint64_t>();

    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t height = blockchain_->getHeight();
    if (fromIndex >= height) {
        // Peer is up to date — send empty response
        Message resp;
        resp.type = MessageType::BLOCKS;
        resp.payload = {{"blocks", nlohmann::json::array()}};
        from->send(resp);
        return;
    }

    // Send blocks from 'fromIndex' to our tip
    nlohmann::json blocksArray = nlohmann::json::array();
    for (uint64_t i = fromIndex; i < height; ++i) {
        blocksArray.push_back(core::toJson(blockchain_->getBlock(i)));
    }

    Message resp;
    resp.type = MessageType::BLOCKS;
    resp.payload = {{"blocks", blocksArray}};
    from->send(resp);

    spdlog::info("[Node] Sent {} blocks to peer (from index {})",
                 blocksArray.size(), fromIndex);
}

void Node::onBlocks(const nlohmann::json& payload) {
    auto blocksArray = payload.at("blocks");

    std::lock_guard<std::mutex> lock(mutex_);

    int applied = 0;
    for (const auto& blockJson : blocksArray) {
        core::Block block = core::blockFromJson(blockJson);

        // Skip blocks we already have
        if (block.index < blockchain_->getHeight()) {
            continue;
        }

        if (block.index != blockchain_->getHeight()) {
            spdlog::warn("[Node] Sync gap: expected block {}, got {}",
                         blockchain_->getHeight(), block.index);
            break;
        }

        if (!blockchain_->addBlock(block)) {
            spdlog::warn("[Node] Sync: rejected block {}: {}",
                         block.index, blockchain_->getLastError());
            break;
        }

        state_->applyBlock(block);
        applied++;
    }

    if (applied > 0) {
        spdlog::info("[Node] Synced {} blocks (height: {})",
                     applied, blockchain_->getHeight());
    }
}

void Node::onPeerConnected(PeerSession::Ptr peer) {
    // When a new peer connects, request their blocks from our height
    std::lock_guard<std::mutex> lock(mutex_);

    Message getBlocks;
    getBlocks.type = MessageType::GET_BLOCKS;
    getBlocks.payload = {{"from", blockchain_->getHeight()}};
    peer->send(getBlocks);

    spdlog::info("[Node] Requesting sync from new peer (our height: {})",
                 blockchain_->getHeight());
}

} // namespace net
} // namespace titancore
