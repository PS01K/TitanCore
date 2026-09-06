// =============================================================================
// TitanCore — Message Serialization Tests
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/net/message.hpp"

using namespace titancore;
using namespace titancore::net;

// =============================================================================
// MessageType String Conversion
// =============================================================================

TEST(MessageTest, TypeToString) {
    EXPECT_EQ(messageTypeToString(MessageType::PING), "PING");
    EXPECT_EQ(messageTypeToString(MessageType::PONG), "PONG");
    EXPECT_EQ(messageTypeToString(MessageType::NEW_BLOCK), "NEW_BLOCK");
    EXPECT_EQ(messageTypeToString(MessageType::NEW_TX), "NEW_TX");
    EXPECT_EQ(messageTypeToString(MessageType::GET_BLOCKS), "GET_BLOCKS");
    EXPECT_EQ(messageTypeToString(MessageType::BLOCKS), "BLOCKS");
}

TEST(MessageTest, TypeFromString) {
    EXPECT_EQ(messageTypeFromString("PING"), MessageType::PING);
    EXPECT_EQ(messageTypeFromString("PONG"), MessageType::PONG);
    EXPECT_EQ(messageTypeFromString("NEW_BLOCK"), MessageType::NEW_BLOCK);
    EXPECT_EQ(messageTypeFromString("NEW_TX"), MessageType::NEW_TX);
    EXPECT_EQ(messageTypeFromString("GET_BLOCKS"), MessageType::GET_BLOCKS);
    EXPECT_EQ(messageTypeFromString("BLOCKS"), MessageType::BLOCKS);
}

TEST(MessageTest, UnknownTypeThrows) {
    EXPECT_THROW(messageTypeFromString("INVALID"), std::invalid_argument);
}

// =============================================================================
// Length Encoding
// =============================================================================

TEST(MessageTest, EncodeLengthZero) {
    uint8_t buf[4];
    encodeLength(0, buf);
    EXPECT_EQ(decodeLength(buf), 0);
}

TEST(MessageTest, EncodeLengthSmall) {
    uint8_t buf[4];
    encodeLength(42, buf);
    EXPECT_EQ(decodeLength(buf), 42);
}

TEST(MessageTest, EncodeLengthLarge) {
    uint8_t buf[4];
    encodeLength(1'000'000, buf);
    EXPECT_EQ(decodeLength(buf), 1'000'000);
}

TEST(MessageTest, EncodeLengthMax) {
    uint8_t buf[4];
    encodeLength(0xFFFFFFFF, buf);
    EXPECT_EQ(decodeLength(buf), 0xFFFFFFFF);
}

TEST(MessageTest, LittleEndianByte0) {
    // 0x04030201 in LE: 01 02 03 04
    uint8_t buf[4];
    encodeLength(0x04030201, buf);
    EXPECT_EQ(buf[0], 0x01);
    EXPECT_EQ(buf[1], 0x02);
    EXPECT_EQ(buf[2], 0x03);
    EXPECT_EQ(buf[3], 0x04);
}

// =============================================================================
// Serialization Round-Trip
// =============================================================================

TEST(MessageTest, SerializePing) {
    Message msg{MessageType::PING, {}};
    Bytes data = serializeMessage(msg);

    // First 4 bytes are the length prefix
    uint32_t length = decodeLength(data.data());
    EXPECT_EQ(data.size(), 4 + length);

    // Deserialize the body
    std::string body(data.begin() + 4, data.end());
    Message parsed = deserializeMessage(body);
    EXPECT_EQ(parsed.type, MessageType::PING);
}

TEST(MessageTest, SerializeNewBlock) {
    nlohmann::json blockPayload;
    blockPayload["index"] = 1;
    blockPayload["hash"] = "abc123";

    Message msg{MessageType::NEW_BLOCK, blockPayload};
    Bytes data = serializeMessage(msg);

    std::string body(data.begin() + 4, data.end());
    Message parsed = deserializeMessage(body);
    EXPECT_EQ(parsed.type, MessageType::NEW_BLOCK);
    EXPECT_EQ(parsed.payload["index"], 1);
    EXPECT_EQ(parsed.payload["hash"], "abc123");
}

TEST(MessageTest, SerializeGetBlocks) {
    nlohmann::json payload;
    payload["from"] = 5;

    Message msg{MessageType::GET_BLOCKS, payload};
    Bytes data = serializeMessage(msg);

    std::string body(data.begin() + 4, data.end());
    Message parsed = deserializeMessage(body);
    EXPECT_EQ(parsed.type, MessageType::GET_BLOCKS);
    EXPECT_EQ(parsed.payload["from"], 5);
}

TEST(MessageTest, SerializeEmptyPayload) {
    Message msg{MessageType::PONG, {}};
    Bytes data = serializeMessage(msg);

    std::string body(data.begin() + 4, data.end());
    Message parsed = deserializeMessage(body);
    EXPECT_EQ(parsed.type, MessageType::PONG);
    EXPECT_TRUE(parsed.payload.is_object());
}

TEST(MessageTest, LengthPrefixMatchesBody) {
    Message msg{MessageType::NEW_BLOCK, {{"data", "test"}}};
    Bytes data = serializeMessage(msg);

    uint32_t declaredLen = decodeLength(data.data());
    uint32_t actualLen = static_cast<uint32_t>(data.size() - 4);
    EXPECT_EQ(declaredLen, actualLen);
}
