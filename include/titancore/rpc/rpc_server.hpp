#pragma once

// =============================================================================
// TitanCore — JSON-RPC Server
// =============================================================================
//
// An HTTP server that exposes the blockchain node's functionality via
// JSON-RPC 2.0. External clients (wallets, scripts, dashboards) can
// query the blockchain and submit transactions without joining the
// P2P network.
//
// ARCHITECTURE:
//   The RPC server runs on its own thread, separate from both the main
//   thread and the P2P networking thread. It holds a reference to the
//   Node and calls its thread-safe query methods.
//
//   Client  ──HTTP POST──▸  RpcServer  ──method call──▸  Node
//                                                         │
//                           JSON-RPC response  ◂──────────┘
//
// SUPPORTED METHODS:
//   getBalance        — query an account's ODM balance
//   getNonce          — query an account's transaction nonce
//   getChainHeight    — current chain tip height
//   getBlockByIndex   — fetch a block by its index
//   getMempoolSize    — number of pending transactions
//   getPeerCount      — number of connected P2P peers
//   submitTransaction — submit a pre-signed transaction
//
// ENDPOINT:
//   POST /rpc  (all methods use the same endpoint)
//
// JSON-RPC 2.0 FORMAT:
//   Request:  {"jsonrpc":"2.0","method":"getBalance","params":{"address":"..."},"id":1}
//   Response: {"jsonrpc":"2.0","result":{...},"id":1}
//   Error:    {"jsonrpc":"2.0","error":{"code":-32601,"message":"..."},"id":1}
// =============================================================================

#include "titancore/net/node.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

// Forward declare to avoid including httplib.h in the header
namespace httplib { class Server; }

namespace titancore {
namespace rpc {

class RpcServer {
public:
    // Create an RPC server bound to the given node and port.
    // The node must outlive the RpcServer.
    RpcServer(net::Node& node, uint16_t port);
    ~RpcServer();

    // Non-copyable
    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;

    // Start the HTTP server on a background thread.
    void start();

    // Stop the HTTP server and join the thread.
    void stop();

    uint16_t getPort() const { return port_; }

private:
    // Handle a POST /rpc request.
    void handleRpc(const std::string& body, std::string& response);

    // Dispatch a JSON-RPC method call.
    nlohmann::json dispatch(const std::string& method,
                            const nlohmann::json& params);

    // Individual method handlers.
    nlohmann::json handleGetBalance(const nlohmann::json& params);
    nlohmann::json handleGetNonce(const nlohmann::json& params);
    nlohmann::json handleGetChainHeight(const nlohmann::json& params);
    nlohmann::json handleGetBlockByIndex(const nlohmann::json& params);
    nlohmann::json handleGetMempoolSize(const nlohmann::json& params);
    nlohmann::json handleGetPeerCount(const nlohmann::json& params);
    nlohmann::json handleSubmitTransaction(const nlohmann::json& params);

    // Build a JSON-RPC success response.
    static nlohmann::json successResponse(const nlohmann::json& result,
                                           const nlohmann::json& id);

    // Build a JSON-RPC error response.
    static nlohmann::json errorResponse(int code, const std::string& message,
                                         const nlohmann::json& id);

    net::Node& node_;
    uint16_t port_;
    std::unique_ptr<httplib::Server> server_;
    std::thread httpThread_;
};

} // namespace rpc
} // namespace titancore
