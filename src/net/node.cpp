#include "titancore/net/node.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/transaction.hpp"
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
           uint16_t listenPort,
           const std::string& dataDir)
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

    // Create mempool (validates against state)
    mempool_ = std::make_unique<core::Mempool>(*state_);

    // Create persistent storage (optional)
    if (!dataDir.empty()) {
        storage_ = std::make_unique<storage::Storage>(dataDir);
        recoverFromStorage();
    }

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
// Storage Recovery
// =============================================================================

void Node::recoverFromStorage() {
    if (!storage_) return;

    uint64_t storedHeight = storage_->loadChainHeight();
    if (storedHeight <= 1) {
        // No stored blocks beyond genesis — nothing to recover
        spdlog::info("[Node] No stored chain found, starting fresh");
        return;
    }

    spdlog::info("[Node] Recovering chain from storage ({} blocks)...",
                 storedHeight);

    // Load and replay blocks 1..(storedHeight-1) — genesis (index 0)
    // is already created by the Blockchain constructor.
    uint64_t recovered = 0;
    for (uint64_t i = 1; i < storedHeight; ++i) {
        auto blockOpt = storage_->loadBlock(i);
        if (!blockOpt) {
            spdlog::warn("[Node] Missing block {} in storage, stopping recovery at {}",
                         i, blockchain_->getHeight());
            break;
        }

        if (!blockchain_->addBlock(*blockOpt)) {
            spdlog::warn("[Node] Failed to replay block {}: {}",
                         i, blockchain_->getLastError());
            break;
        }

        state_->applyBlock(*blockOpt);
        recovered++;
    }

    spdlog::info("[Node] Recovered {} blocks (chain height: {})",
                 recovered, blockchain_->getHeight());
}

// =============================================================================
// Lifecycle
// =============================================================================

void Node::start() {
    network_->start();
    spdlog::info("[Node] Started (chain height: {}, mempool: {})",
                 blockchain_->getHeight(), mempool_->size());
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
// Transaction Submission
// =============================================================================

bool Node::submitTransaction(const core::Transaction& tx) {
    Message broadcastMsg;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!mempool_->addTransaction(tx)) {
            lastError_ = mempool_->getLastError();
            spdlog::debug("[Node] Transaction rejected: {}", lastError_);
            return false;
        }

        spdlog::info("[Node] Transaction accepted into mempool (pending: {})",
                     mempool_->size());

        // Prepare broadcast
        broadcastMsg.type = MessageType::NEW_TX;
        broadcastMsg.payload = core::toJson(tx);
    }

    // Broadcast to peers (outside the lock)
    network_->broadcast(broadcastMsg);
    return true;
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

        // Pull transactions from the mempool (up to 100 per block)
        auto txs = mempool_->getTransactionsForBlock(100);

        // Create the block with real transactions
        core::Block block = core::createBlock(
            nodeKeys_, blockchain_->getLatestBlock(), txs);

        // Add to our chain
        if (!blockchain_->addBlock(block)) {
            spdlog::warn("[Node] Failed to add own block: {}",
                         blockchain_->getLastError());
            return false;
        }

        // Apply to state + post-block cleanup
        state_->applyBlock(block);
        afterBlockAccepted(block);

        spdlog::info("[Node] Produced block {}: {} ({} transactions)",
                     block.index, crypto::toHex(block.hash),
                     block.transactions.size());

        // Prepare broadcast message
        broadcastMsg.type = MessageType::NEW_BLOCK;
        broadcastMsg.payload = core::toJson(block);
    }

    // Broadcast to peers (no lock needed — NetworkManager has its own)
    network_->broadcast(broadcastMsg);
    return true;
}

// =============================================================================
// Post-Block Cleanup
// =============================================================================

void Node::afterBlockAccepted(const core::Block& block) {
    // Remove mined transactions from the mempool
    mempool_->removeMinedTransactions(block);
    size_t evicted = mempool_->revalidate();
    if (evicted > 0) {
        spdlog::info("[Node] Revalidation evicted {} transactions", evicted);
    }

    // Persist to LevelDB (if enabled)
    if (storage_) {
        storage_->saveBlock(block);
        storage_->saveChainHeight(blockchain_->getHeight());

        // Save account states for all senders and receivers in this block
        for (const auto& tx : block.transactions) {
            Address sender = crypto::deriveAddress(tx.senderPublicKey);
            if (state_->accountExists(sender)) {
                core::AccountState acct;
                acct.balance = state_->getBalance(sender);
                acct.nonce = state_->getNonce(sender);
                storage_->saveAccountState(sender, acct);
            }
            if (state_->accountExists(tx.recipient)) {
                core::AccountState acct;
                acct.balance = state_->getBalance(tx.recipient);
                acct.nonce = state_->getNonce(tx.recipient);
                storage_->saveAccountState(tx.recipient, acct);
            }
        }
    }
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

size_t Node::getMempoolSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mempool_->size();
}

uint64_t Node::getBalance(const Address& addr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_->accountExists(addr)) return 0;
    return state_->getBalance(addr);
}

uint64_t Node::getNonce(const Address& addr) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_->accountExists(addr)) return 0;
    return state_->getNonce(addr);
}

std::string Node::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
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

        case MessageType::NEW_TX:
            onNewTx(msg.payload, from);
            break;

        case MessageType::GET_BLOCKS:
            onGetBlocks(msg.payload, from);
            break;

        case MessageType::BLOCKS:
            onBlocks(msg.payload);
            break;
    }
}

void Node::onNewTx(const nlohmann::json& payload, PeerSession::Ptr from) {
    core::Transaction tx = core::transactionFromJson(payload);

    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (mempool_->contains(tx.hash)) {
            // Already have this transaction — ignore
            return;
        }

        if (mempool_->addTransaction(tx)) {
            accepted = true;
            spdlog::debug("[Node] Accepted tx from peer (mempool: {})",
                          mempool_->size());
        } else {
            spdlog::debug("[Node] Rejected tx from peer: {}",
                          mempool_->getLastError());
        }
    }

    // Rebroadcast to other peers (outside the lock)
    if (accepted) {
        Message fwd;
        fwd.type = MessageType::NEW_TX;
        fwd.payload = payload;
        network_->broadcast(fwd, from);
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
        afterBlockAccepted(block);
        accepted = true;

        spdlog::info("[Node] Accepted block {} from peer ({} txs, height: {})",
                     block.index, block.transactions.size(),
                     blockchain_->getHeight());
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
        afterBlockAccepted(block);
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
