#pragma once

// =============================================================================
// TitanCore — Network Manager
// =============================================================================
//
// The NetworkManager handles all TCP networking:
//   - Listens for incoming peer connections (server mode)
//   - Connects to known peers (client mode)
//   - Broadcasts messages to all connected peers
//   - Runs the ASIO io_context in a separate thread
//
// THREADING MODEL:
//   The io_context event loop runs in its own std::thread. This means
//   the main thread can continue interacting with the node (producing
//   blocks, querying state) while networking happens in the background.
//
//   All ASIO callbacks execute on the io_context thread. The Node class
//   uses a mutex to protect shared state accessed from both threads.
//
// WORK GUARD:
//   The io_context normally exits run() when there's no pending work.
//   We use a work_guard to keep it alive even when idle (waiting for
//   incoming connections counts as pending work, but we add the guard
//   as a safety net).
// =============================================================================

#include "titancore/net/peer_session.hpp"
#include "titancore/net/message.hpp"

#include <asio.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace titancore {
namespace net {

class NetworkManager {
public:
    using MessageCallback = std::function<void(const Message&, PeerSession::Ptr)>;
    using PeerCallback = std::function<void(PeerSession::Ptr)>;

    // Create a network manager that listens on the given port.
    explicit NetworkManager(uint16_t port);
    ~NetworkManager();

    // Non-copyable, non-movable (owns thread + io_context)
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    // Start the network thread and begin accepting connections.
    void start();

    // Stop the network thread and close all connections.
    void stop();

    // =========================================================================
    // Peer Management
    // =========================================================================

    // Connect to a peer at the given host:port.
    // The connection is established asynchronously.
    void connectToPeer(const std::string& host, uint16_t port);

    // Send a message to all connected peers.
    // Optionally exclude one peer (e.g., the sender of a received message).
    void broadcast(const Message& msg,
                   PeerSession::Ptr exclude = nullptr);

    // Get the number of connected peers.
    size_t peerCount() const;

    // Get the actual listening port (useful when port 0 is used for testing).
    uint16_t getListenPort() const;

    // =========================================================================
    // Callbacks
    // =========================================================================

    // Set the callback for incoming messages.
    void setMessageCallback(MessageCallback cb);

    // Set the callback for new peer connections.
    void setOnPeerConnected(PeerCallback cb);

private:
    // Accept loop: accept → create session → accept next
    void startAccepting();

    // Add/remove peers from the active list
    void addPeer(PeerSession::Ptr session);
    void removePeer(PeerSession::Ptr session);

    // ASIO components
    asio::io_context ioContext_;
    asio::executor_work_guard<asio::io_context::executor_type> workGuard_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread networkThread_;

    // Active peer connections
    mutable std::mutex peersMutex_;
    std::vector<PeerSession::Ptr> peers_;

    // Callbacks
    MessageCallback messageCallback_;
    PeerCallback onPeerConnected_;
};

} // namespace net
} // namespace titancore
