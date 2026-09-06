#include "titancore/net/network_manager.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace titancore {
namespace net {

NetworkManager::NetworkManager(uint16_t port)
    : workGuard_(asio::make_work_guard(ioContext_))
    , acceptor_(ioContext_,
                asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    spdlog::info("[Net] Listening on port {}", acceptor_.local_endpoint().port());
}

NetworkManager::~NetworkManager() {
    stop();
}

// =============================================================================
// Lifecycle
// =============================================================================

void NetworkManager::start() {
    startAccepting();
    networkThread_ = std::thread([this]() {
        spdlog::debug("[Net] io_context thread started");
        ioContext_.run();
        spdlog::debug("[Net] io_context thread finished");
    });
}

void NetworkManager::stop() {
    // Close all peer connections
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        for (auto& peer : peers_) {
            peer->close();
        }
        peers_.clear();
    }

    // Stop the io_context (causes run() to return)
    workGuard_.reset();
    ioContext_.stop();

    // Wait for the network thread to finish
    if (networkThread_.joinable()) {
        networkThread_.join();
    }

    spdlog::info("[Net] Network manager stopped");
}

// =============================================================================
// Accept Loop
// =============================================================================

void NetworkManager::startAccepting() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    spdlog::warn("[Net] Accept error: {}", ec.message());
                }
                return;  // Don't re-accept on abort (we're shutting down)
            }

            // Create a PeerSession for the new connection
            auto session = std::make_shared<PeerSession>(
                std::move(socket),
                // Message callback — forward to our callback
                [this](const Message& msg, PeerSession::Ptr from) {
                    if (messageCallback_) {
                        messageCallback_(msg, from);
                    }
                },
                // Disconnect callback — remove from our list
                [this](PeerSession::Ptr peer) {
                    removePeer(peer);
                }
            );

            addPeer(session);
            session->start();

            if (onPeerConnected_) {
                onPeerConnected_(session);
            }

            // Accept the next connection
            startAccepting();
        });
}

// =============================================================================
// Peer Management
// =============================================================================

void NetworkManager::connectToPeer(const std::string& host, uint16_t port) {
    // Resolve and connect asynchronously
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(ioContext_);

    resolver->async_resolve(host, std::to_string(port),
        [this, resolver, host, port](
            std::error_code ec,
            asio::ip::tcp::resolver::results_type results) {

            if (ec) {
                spdlog::warn("[Net] Failed to resolve {}:{}: {}",
                             host, port, ec.message());
                return;
            }

            auto socket = std::make_shared<asio::ip::tcp::socket>(ioContext_);
            asio::async_connect(*socket, results,
                [this, socket, host, port](
                    std::error_code ec,
                    const asio::ip::tcp::endpoint& /*endpoint*/) {

                    if (ec) {
                        spdlog::warn("[Net] Failed to connect to {}:{}: {}",
                                     host, port, ec.message());
                        return;
                    }

                    // Create a PeerSession for the outbound connection
                    auto session = std::make_shared<PeerSession>(
                        std::move(*socket),
                        [this](const Message& msg, PeerSession::Ptr from) {
                            if (messageCallback_) {
                                messageCallback_(msg, from);
                            }
                        },
                        [this](PeerSession::Ptr peer) {
                            removePeer(peer);
                        }
                    );

                    addPeer(session);
                    session->start();

                    if (onPeerConnected_) {
                        onPeerConnected_(session);
                    }

                    spdlog::info("[Net] Connected to {}:{}", host, port);
                });
        });
}

void NetworkManager::broadcast(const Message& msg, PeerSession::Ptr exclude) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    for (auto& peer : peers_) {
        if (peer != exclude) {
            peer->send(msg);
        }
    }
}

size_t NetworkManager::peerCount() const {
    std::lock_guard<std::mutex> lock(peersMutex_);
    return peers_.size();
}

uint16_t NetworkManager::getListenPort() const {
    return acceptor_.local_endpoint().port();
}

// =============================================================================
// Callbacks
// =============================================================================

void NetworkManager::setMessageCallback(MessageCallback cb) {
    messageCallback_ = std::move(cb);
}

void NetworkManager::setOnPeerConnected(PeerCallback cb) {
    onPeerConnected_ = std::move(cb);
}

// =============================================================================
// Internal Peer List
// =============================================================================

void NetworkManager::addPeer(PeerSession::Ptr session) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    peers_.push_back(session);
    spdlog::info("[Net] Peer added (total: {})", peers_.size());
}

void NetworkManager::removePeer(PeerSession::Ptr session) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    peers_.erase(
        std::remove(peers_.begin(), peers_.end(), session),
        peers_.end());
    spdlog::info("[Net] Peer removed (total: {})", peers_.size());
}

} // namespace net
} // namespace titancore
