// =============================================================================
// TitanCore — RPC Server Tests
// =============================================================================
//
// Tests the JSON-RPC 2.0 server by starting a node with an RPC server,
// sending HTTP requests, and verifying the responses.
// =============================================================================

#include "titancore/rpc/rpc_server.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/net/node.hpp"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <thread>

using namespace titancore;
using namespace titancore::crypto;
using namespace titancore::core;
using namespace titancore::net;
using namespace titancore::rpc;
using json = nlohmann::json;

// =============================================================================
// Test Fixture
// =============================================================================

class RpcServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create 2 authorities
        auth0_ = generateKeyPair();
        auth1_ = generateKeyPair();
        addr0_ = deriveAddress(auth0_.publicKey);
        addr1_ = deriveAddress(auth1_.publicKey);

        std::vector<Address> authorities = {addr0_, addr1_};
        AddressMap<uint64_t> allocations;
        allocations[addr0_] = 10000;
        allocations[addr1_] = 5000;

        // Create node (in-memory, no LevelDB)
        node_ = std::make_unique<Node>(auth0_, auth0_, authorities,
                                        allocations, rpcTestP2PPort_);
        node_->start();

        // Start RPC server
        rpc_ = std::make_unique<RpcServer>(*node_, rpcTestPort_);
        rpc_->start();

        // Give the HTTP server time to bind
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Create HTTP client
        client_ = std::make_unique<httplib::Client>("localhost", rpcTestPort_);
    }

    void TearDown() override {
        client_.reset();
        rpc_->stop();
        node_.reset();
    }

    // Helper to send a JSON-RPC request and parse the response
    json rpcCall(const std::string& method,
                 const json& params = json::object(),
                 int id = 1) {
        json req = {
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", params},
            {"id", id}
        };

        auto res = client_->Post("/rpc", req.dump(), "application/json");
        EXPECT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 200);

        return json::parse(res->body);
    }

    KeyPair auth0_, auth1_;
    Address addr0_, addr1_;
    std::unique_ptr<Node> node_;
    std::unique_ptr<RpcServer> rpc_;
    std::unique_ptr<httplib::Client> client_;

    // Use high ports to avoid conflicts with other tests
    static constexpr uint16_t rpcTestPort_ = 18545;
    static constexpr uint16_t rpcTestP2PPort_ = 19001;
};

// =============================================================================
// Tests
// =============================================================================

TEST_F(RpcServerTest, GetChainHeight) {
    json res = rpcCall("getChainHeight");
    EXPECT_EQ(res["jsonrpc"], "2.0");
    EXPECT_EQ(res["id"], 1);
    EXPECT_EQ(res["result"]["height"], 1);  // genesis only
}

TEST_F(RpcServerTest, GetBalance) {
    json res = rpcCall("getBalance", {{"address", toHex(addr0_)}});
    EXPECT_EQ(res["result"]["balance"], 10000);

    json res2 = rpcCall("getBalance", {{"address", toHex(addr1_)}});
    EXPECT_EQ(res2["result"]["balance"], 5000);
}

TEST_F(RpcServerTest, GetNonce) {
    json res = rpcCall("getNonce", {{"address", toHex(addr0_)}});
    EXPECT_EQ(res["result"]["nonce"], 0);
}

TEST_F(RpcServerTest, GetMempoolSize) {
    json res = rpcCall("getMempoolSize");
    EXPECT_EQ(res["result"]["size"], 0);
}

TEST_F(RpcServerTest, GetPeerCount) {
    json res = rpcCall("getPeerCount");
    EXPECT_EQ(res["result"]["count"], 0);
}

TEST_F(RpcServerTest, GetBlockByIndex) {
    json res = rpcCall("getBlockByIndex", {{"index", 0}});
    ASSERT_TRUE(res.contains("result"));
    ASSERT_TRUE(res["result"].contains("block"));

    json block = res["result"]["block"];
    EXPECT_EQ(block["index"], 0);
    EXPECT_TRUE(block.contains("hash"));
    EXPECT_TRUE(block.contains("validator"));
    EXPECT_TRUE(block.contains("transactions"));
    EXPECT_EQ(block["transactions"].size(), 0);
}

TEST_F(RpcServerTest, GetBlockByIndexOutOfRange) {
    json res = rpcCall("getBlockByIndex", {{"index", 999}});
    ASSERT_TRUE(res.contains("error"));
    EXPECT_EQ(res["error"]["code"], -32602);
}

TEST_F(RpcServerTest, UnknownMethod) {
    json res = rpcCall("nonExistentMethod");
    ASSERT_TRUE(res.contains("error"));
    EXPECT_EQ(res["error"]["code"], -32601);
    EXPECT_TRUE(res["error"]["message"].get<std::string>().find("nonExistentMethod")
                != std::string::npos);
}

TEST_F(RpcServerTest, MissingParams) {
    json res = rpcCall("getBalance", json::object());
    ASSERT_TRUE(res.contains("error"));
    EXPECT_EQ(res["error"]["code"], -32602);
}

TEST_F(RpcServerTest, InvalidJson) {
    auto res = client_->Post("/rpc", "not json at all", "application/json");
    ASSERT_TRUE(res != nullptr);
    json body = json::parse(res->body);
    ASSERT_TRUE(body.contains("error"));
    EXPECT_EQ(body["error"]["code"], -32700);  // Parse error
}

TEST_F(RpcServerTest, SubmitTransaction) {
    // Create and sign a real transaction using the helper
    Transaction tx = createTransaction(auth0_, addr1_, 100, 0);

    json txJson = toJson(tx);
    json res = rpcCall("submitTransaction", {{"transaction", txJson}});

    ASSERT_TRUE(res.contains("result"));
    EXPECT_TRUE(res["result"]["accepted"].get<bool>());
    EXPECT_TRUE(res["result"].contains("hash"));
}

TEST_F(RpcServerTest, SubmitInvalidTransaction) {
    // Submit a transaction with a bogus signature
    json fakeTx = {
        {"sender", toHex(addr0_)},
        {"recipient", toHex(addr1_)},
        {"amount", 100},
        {"nonce", 0},
        {"timestamp", 123},
        {"hash", "0000000000000000000000000000000000000000000000000000000000000000"},
        {"signature", "00"},
        {"senderPublicKey", toHex(auth0_.publicKey)}
    };

    json res = rpcCall("submitTransaction", {{"transaction", fakeTx}});
    // Should either be rejected (accepted: false) or error
    if (res.contains("result")) {
        EXPECT_FALSE(res["result"]["accepted"].get<bool>());
    } else {
        EXPECT_TRUE(res.contains("error"));
    }
}

TEST_F(RpcServerTest, ResponseIdCorrelation) {
    json res = rpcCall("getChainHeight", json::object(), 42);
    EXPECT_EQ(res["id"], 42);
}
