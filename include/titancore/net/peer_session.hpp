#pragma once

// =============================================================================
// TitanCore — Peer Session (Single TCP Connection)
// =============================================================================
//
// A PeerSession manages a single TCP connection to another node.
// It handles:
//   - Async reading with length-prefixed framing
//   - Write queue for serialized outbound messages
//   - Disconnect detection and cleanup
//
// SHARED_FROM_THIS PATTERN:
//   PeerSession inherits from std::enable_shared_from_this. This is
//   essential for ASIO async operations: when you call async_read(),
//   the callback might fire after the PeerSession object is "logically"
//   done. By capturing shared_from_this() in the callback, we keep the
//   object alive until the callback completes. Without this, you'd get
//   use-after-free bugs.
//
// THREAD SAFETY:
//   All PeerSession methods are called from the io_context thread
//   (via async callbacks or asio::post). No mutex is needed inside
//   PeerSession itself — single-threaded access is guaranteed by ASIO's
//   execution model.
// =============================================================================

#include "titancore/net/message.hpp"

#include <asio.hpp>
#include <deque>
#include <functional>
#include <memory>

namespace titancore {
namespace net {

class PeerSession : public std::enable_shared_from_this<PeerSession> {
public:
    using Ptr = std::shared_ptr<PeerSession>;
    using MessageCallback = std::function<void(const Message&, Ptr)>;
    using DisconnectCallback = std::function<void(Ptr)>;

    PeerSession(asio::ip::tcp::socket socket,
                MessageCallback onMessage,
                DisconnectCallback onDisconnect);

    // Start the async read loop. Must be called after construction.
    void start();

    // Queue a message for sending. Thread-safe via asio::post.
    void send(const Message& msg);

    // Close the connection.
    void close();

    // Get the remote endpoint address (for logging).
    std::string remoteAddress() const;

private:
    // Async read loop: header → body → handle → header → ...
    void readHeader();
    void readBody(uint32_t length);

    // Write queue management
    void doWrite();

    // Handle disconnection (error or explicit close)
    void disconnect();

    asio::ip::tcp::socket socket_;
    MessageCallback onMessage_;
    DisconnectCallback onDisconnect_;

    // Read buffers
    std::array<uint8_t, 4> headerBuf_;   // 4-byte length prefix
    std::vector<uint8_t> bodyBuf_;        // Variable-length JSON body

    // Write queue — messages waiting to be sent.
    // Only one async_write can be active at a time (ASIO rule).
    // We queue messages and send them one by one.
    std::deque<Bytes> writeQueue_;
    bool isWriting_ = false;
    bool closed_ = false;
};

} // namespace net
} // namespace titancore
