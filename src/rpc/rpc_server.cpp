#include "titancore/rpc/rpc_server.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"

#include <httplib.h>
#include <spdlog/spdlog.h>

namespace titancore {
namespace rpc {

// =============================================================================
// JSON-RPC 2.0 Error Codes (standard)
// =============================================================================
static constexpr int PARSE_ERROR      = -32700;
static constexpr int INVALID_REQUEST  = -32600;
static constexpr int METHOD_NOT_FOUND = -32601;
static constexpr int INVALID_PARAMS   = -32602;
static constexpr int INTERNAL_ERROR   = -32603;

// =============================================================================
// Construction / Destruction
// =============================================================================

RpcServer::RpcServer(net::Node& node, uint16_t port)
    : node_(node)
    , port_(port)
    , server_(std::make_unique<httplib::Server>()) {
}

RpcServer::~RpcServer() {
    stop();
}

// =============================================================================
// Lifecycle
// =============================================================================

void RpcServer::start() {
    // Register the /rpc endpoint
    server_->Post("/rpc", [this](const httplib::Request& req,
                                  httplib::Response& res) {
        std::string responseBody;
        handleRpc(req.body, responseBody);
        res.set_content(responseBody, "application/json");
    });

    // Start on a background thread
    httpThread_ = std::thread([this]() {
        spdlog::info("[RPC] HTTP server listening on port {}", port_);
        server_->listen("0.0.0.0", port_);
        spdlog::info("[RPC] HTTP server stopped");
    });
}

void RpcServer::stop() {
    if (server_->is_running()) {
        server_->stop();
    }
    if (httpThread_.joinable()) {
        httpThread_.join();
    }
}

// =============================================================================
// Request Handling
// =============================================================================

void RpcServer::handleRpc(const std::string& body, std::string& response) {
    nlohmann::json id = nullptr;

    try {
        // Parse the request body
        nlohmann::json req = nlohmann::json::parse(body);

        // Extract the id (for correlating request/response)
        if (req.contains("id")) {
            id = req["id"];
        }

        // Validate JSON-RPC 2.0 structure
        if (!req.contains("method") || !req["method"].is_string()) {
            response = errorResponse(INVALID_REQUEST,
                                     "Missing or invalid 'method' field", id).dump();
            return;
        }

        std::string method = req["method"].get<std::string>();
        nlohmann::json params = req.value("params", nlohmann::json::object());

        spdlog::debug("[RPC] {} (id={})", method, id.dump());

        // Dispatch
        nlohmann::json result = dispatch(method, params);

        // Check if dispatch returned an error object
        if (result.contains("__rpc_error")) {
            int code = result["__rpc_error"]["code"].get<int>();
            std::string msg = result["__rpc_error"]["message"].get<std::string>();
            response = errorResponse(code, msg, id).dump();
        } else {
            response = successResponse(result, id).dump();
        }

    } catch (const nlohmann::json::parse_error& e) {
        response = errorResponse(PARSE_ERROR,
                                 std::string("JSON parse error: ") + e.what(), id).dump();
    } catch (const std::exception& e) {
        response = errorResponse(INTERNAL_ERROR,
                                 std::string("Internal error: ") + e.what(), id).dump();
    }
}

nlohmann::json RpcServer::dispatch(const std::string& method,
                                    const nlohmann::json& params) {
    if (method == "getBalance")        return handleGetBalance(params);
    if (method == "getNonce")          return handleGetNonce(params);
    if (method == "getChainHeight")    return handleGetChainHeight(params);
    if (method == "getBlockByIndex")   return handleGetBlockByIndex(params);
    if (method == "getMempoolSize")    return handleGetMempoolSize(params);
    if (method == "getPeerCount")      return handleGetPeerCount(params);
    if (method == "submitTransaction") return handleSubmitTransaction(params);

    // Unknown method
    nlohmann::json err;
    err["__rpc_error"] = {
        {"code", METHOD_NOT_FOUND},
        {"message", "Unknown method: " + method}
    };
    return err;
}

// =============================================================================
// Method Handlers
// =============================================================================

nlohmann::json RpcServer::handleGetBalance(const nlohmann::json& params) {
    if (!params.contains("address") || !params["address"].is_string()) {
        nlohmann::json err;
        err["__rpc_error"] = {
            {"code", INVALID_PARAMS},
            {"message", "Missing 'address' parameter"}
        };
        return err;
    }

    Address addr = crypto::fromHexFixed<20>(params["address"].get<std::string>());
    uint64_t balance = node_.getBalance(addr);

    return {{"balance", balance}};
}

nlohmann::json RpcServer::handleGetNonce(const nlohmann::json& params) {
    if (!params.contains("address") || !params["address"].is_string()) {
        nlohmann::json err;
        err["__rpc_error"] = {
            {"code", INVALID_PARAMS},
            {"message", "Missing 'address' parameter"}
        };
        return err;
    }

    Address addr = crypto::fromHexFixed<20>(params["address"].get<std::string>());
    uint64_t nonce = node_.getNonce(addr);

    return {{"nonce", nonce}};
}

nlohmann::json RpcServer::handleGetChainHeight(const nlohmann::json& /*params*/) {
    uint64_t height = node_.getChainHeight();
    return {{"height", height}};
}

nlohmann::json RpcServer::handleGetBlockByIndex(const nlohmann::json& params) {
    if (!params.contains("index") || !params["index"].is_number_unsigned()) {
        nlohmann::json err;
        err["__rpc_error"] = {
            {"code", INVALID_PARAMS},
            {"message", "Missing or invalid 'index' parameter"}
        };
        return err;
    }

    uint64_t index = params["index"].get<uint64_t>();

    try {
        core::Block block = node_.getBlock(index);
        return {{"block", core::toJson(block)}};
    } catch (const std::out_of_range& e) {
        nlohmann::json err;
        err["__rpc_error"] = {
            {"code", INVALID_PARAMS},
            {"message", std::string("Block not found: ") + e.what()}
        };
        return err;
    }
}

nlohmann::json RpcServer::handleGetMempoolSize(const nlohmann::json& /*params*/) {
    size_t size = node_.getMempoolSize();
    return {{"size", size}};
}

nlohmann::json RpcServer::handleGetPeerCount(const nlohmann::json& /*params*/) {
    size_t count = node_.getPeerCount();
    return {{"count", count}};
}

nlohmann::json RpcServer::handleSubmitTransaction(const nlohmann::json& params) {
    if (!params.contains("transaction") || !params["transaction"].is_object()) {
        nlohmann::json err;
        err["__rpc_error"] = {
            {"code", INVALID_PARAMS},
            {"message", "Missing 'transaction' object"}
        };
        return err;
    }

    try {
        core::Transaction tx = core::transactionFromJson(params["transaction"]);
        bool accepted = node_.submitTransaction(tx);

        if (accepted) {
            return {
                {"accepted", true},
                {"hash", crypto::toHex(tx.hash)}
            };
        } else {
            return {
                {"accepted", false},
                {"error", node_.getLastError()}
            };
        }
    } catch (const std::exception& e) {
        nlohmann::json err;
        err["__rpc_error"] = {
            {"code", INVALID_PARAMS},
            {"message", std::string("Invalid transaction: ") + e.what()}
        };
        return err;
    }
}

// =============================================================================
// Response Builders
// =============================================================================

nlohmann::json RpcServer::successResponse(const nlohmann::json& result,
                                           const nlohmann::json& id) {
    return {
        {"jsonrpc", "2.0"},
        {"result", result},
        {"id", id}
    };
}

nlohmann::json RpcServer::errorResponse(int code, const std::string& message,
                                         const nlohmann::json& id) {
    spdlog::warn("[RPC] Error {}: {}", code, message);
    return {
        {"jsonrpc", "2.0"},
        {"error", {{"code", code}, {"message", message}}},
        {"id", id}
    };
}

} // namespace rpc
} // namespace titancore
