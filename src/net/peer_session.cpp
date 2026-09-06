#include "titancore/net/peer_session.hpp"
#include <spdlog/spdlog.h>

namespace titancore {
namespace net {

PeerSession::PeerSession(asio::ip::tcp::socket socket,
                         MessageCallback onMessage,
                         DisconnectCallback onDisconnect)
    : socket_(std::move(socket))
    , onMessage_(std::move(onMessage))
    , onDisconnect_(std::move(onDisconnect)) {}

void PeerSession::start() {
    spdlog::info("[Net] Peer connected: {}", remoteAddress());
    readHeader();
}

void PeerSession::send(const Message& msg) {
    // Marshal to the io_context thread via post.
    // This ensures all write queue operations happen on one thread.
    auto data = serializeMessage(msg);
    asio::post(socket_.get_executor(),
        [self = shared_from_this(), data = std::move(data)]() mutable {
            if (self->closed_) return;
            self->writeQueue_.push_back(std::move(data));
            if (!self->isWriting_) {
                self->doWrite();
            }
        });
}

void PeerSession::close() {
    asio::post(socket_.get_executor(),
        [self = shared_from_this()]() {
            self->disconnect();
        });
}

std::string PeerSession::remoteAddress() const {
    try {
        auto ep = socket_.remote_endpoint();
        return ep.address().to_string() + ":" + std::to_string(ep.port());
    } catch (...) {
        return "<disconnected>";
    }
}

// =============================================================================
// Read Loop
// =============================================================================

void PeerSession::readHeader() {
    // Read exactly 4 bytes — the length prefix.
    asio::async_read(socket_, asio::buffer(headerBuf_),
        [self = shared_from_this()](std::error_code ec, size_t /*bytes*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    spdlog::debug("[Net] Read error from {}: {}",
                                  self->remoteAddress(), ec.message());
                }
                self->disconnect();
                return;
            }
            uint32_t bodyLen = decodeLength(self->headerBuf_.data());

            // Sanity check: reject absurdly large messages (> 16 MB)
            if (bodyLen > 16 * 1024 * 1024) {
                spdlog::warn("[Net] Message too large ({} bytes), disconnecting {}",
                             bodyLen, self->remoteAddress());
                self->disconnect();
                return;
            }

            self->readBody(bodyLen);
        });
}

void PeerSession::readBody(uint32_t length) {
    bodyBuf_.resize(length);
    asio::async_read(socket_, asio::buffer(bodyBuf_),
        [self = shared_from_this()](std::error_code ec, size_t /*bytes*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    spdlog::debug("[Net] Read error from {}: {}",
                                  self->remoteAddress(), ec.message());
                }
                self->disconnect();
                return;
            }

            // Parse the JSON body into a Message
            try {
                std::string json(self->bodyBuf_.begin(), self->bodyBuf_.end());
                Message msg = deserializeMessage(json);

                spdlog::debug("[Net] Received {} from {}",
                              messageTypeToString(msg.type),
                              self->remoteAddress());

                if (self->onMessage_) {
                    self->onMessage_(msg, self);
                }
            } catch (const std::exception& e) {
                spdlog::warn("[Net] Failed to parse message from {}: {}",
                             self->remoteAddress(), e.what());
            }

            // Continue the read loop
            self->readHeader();
        });
}

// =============================================================================
// Write Queue
// =============================================================================

void PeerSession::doWrite() {
    if (writeQueue_.empty() || closed_) {
        isWriting_ = false;
        return;
    }
    isWriting_ = true;
    asio::async_write(socket_, asio::buffer(writeQueue_.front()),
        [self = shared_from_this()](std::error_code ec, size_t /*bytes*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    spdlog::debug("[Net] Write error to {}: {}",
                                  self->remoteAddress(), ec.message());
                }
                self->disconnect();
                return;
            }
            self->writeQueue_.pop_front();
            self->doWrite();  // Send next queued message
        });
}

// =============================================================================
// Disconnect
// =============================================================================

void PeerSession::disconnect() {
    if (closed_) return;
    closed_ = true;

    std::error_code ec;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    spdlog::info("[Net] Peer disconnected: {}", remoteAddress());

    if (onDisconnect_) {
        onDisconnect_(shared_from_this());
    }
}

} // namespace net
} // namespace titancore
