#include "titancore/net/message.hpp"
#include <stdexcept>

namespace titancore {
namespace net {

std::string messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::PING:       return "PING";
        case MessageType::PONG:       return "PONG";
        case MessageType::NEW_BLOCK:  return "NEW_BLOCK";
        case MessageType::NEW_TX:     return "NEW_TX";
        case MessageType::GET_BLOCKS: return "GET_BLOCKS";
        case MessageType::BLOCKS:     return "BLOCKS";
    }
    return "UNKNOWN";
}

MessageType messageTypeFromString(const std::string& str) {
    if (str == "PING")       return MessageType::PING;
    if (str == "PONG")       return MessageType::PONG;
    if (str == "NEW_BLOCK")  return MessageType::NEW_BLOCK;
    if (str == "NEW_TX")     return MessageType::NEW_TX;
    if (str == "GET_BLOCKS") return MessageType::GET_BLOCKS;
    if (str == "BLOCKS")     return MessageType::BLOCKS;
    throw std::invalid_argument("Unknown message type: " + str);
}

void encodeLength(uint32_t length, uint8_t* buf) {
    buf[0] = static_cast<uint8_t>(length & 0xFF);
    buf[1] = static_cast<uint8_t>((length >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>((length >> 16) & 0xFF);
    buf[3] = static_cast<uint8_t>((length >> 24) & 0xFF);
}

uint32_t decodeLength(const uint8_t* buf) {
    return static_cast<uint32_t>(buf[0])
         | (static_cast<uint32_t>(buf[1]) << 8)
         | (static_cast<uint32_t>(buf[2]) << 16)
         | (static_cast<uint32_t>(buf[3]) << 24);
}

Bytes serializeMessage(const Message& msg) {
    // Build the JSON envelope: {"type": "...", "payload": {...}}
    nlohmann::json j;
    j["type"] = messageTypeToString(msg.type);
    // Ensure payload is always a JSON object (nlohmann::json{} is null)
    j["payload"] = msg.payload.is_null() ? nlohmann::json::object() : msg.payload;
    std::string body = j.dump();

    // Prepend the 4-byte length prefix
    uint32_t length = static_cast<uint32_t>(body.size());
    Bytes data(4 + length);
    encodeLength(length, data.data());
    std::copy(body.begin(), body.end(), data.begin() + 4);
    return data;
}

Message deserializeMessage(const std::string& jsonBody) {
    nlohmann::json j = nlohmann::json::parse(jsonBody);
    Message msg;
    msg.type = messageTypeFromString(j.at("type").get<std::string>());
    msg.payload = j.value("payload", nlohmann::json::object());
    return msg;
}

} // namespace net
} // namespace titancore
