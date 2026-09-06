#pragma once

// =============================================================================
// TitanCore — Network Message Protocol
// =============================================================================
//
// This defines the message format for node-to-node communication.
//
// WIRE FORMAT (Length-Prefixed JSON):
//   Every message on the wire looks like:
//
//     ┌──────────────┬──────────────────────────────────────┐
//     │ 4 bytes (LE) │            JSON body                 │
//     │  msg length  │  {"type":"NEW_BLOCK","payload":{...}} │
//     └──────────────┴──────────────────────────────────────┘
//
//   The 4-byte little-endian uint32 tells the receiver how many bytes
//   to read for the JSON body. This is called "framing" — it solves
//   TCP's fundamental problem that it's a byte stream, not a message
//   stream. Without framing, you don't know where one JSON message
//   ends and the next begins.
//
// WHY JSON?
//   It's human-readable (great for debugging with Wireshark/tcpdump),
//   consistent with our existing serialization, and the performance
//   overhead is negligible for V1 block sizes. Production systems
//   might use Protocol Buffers or a binary format.
// =============================================================================

#include "titancore/common/types.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

namespace titancore {
namespace net {

// =============================================================================
// Message Types
// =============================================================================

enum class MessageType {
    PING,        // Keep-alive check
    PONG,        // Keep-alive response
    NEW_BLOCK,   // Broadcast a newly produced block
    NEW_TX,      // Broadcast a new transaction
    GET_BLOCKS,  // Request blocks starting from an index
    BLOCKS       // Response with an array of blocks
};

// Convert between enum and string for JSON serialization
std::string messageTypeToString(MessageType type);
MessageType messageTypeFromString(const std::string& str);

// =============================================================================
// Message Struct
// =============================================================================

struct Message {
    MessageType type;
    nlohmann::json payload;  // Type-specific data
};

// =============================================================================
// Serialization
// =============================================================================

// Serialize a Message to wire format: [4-byte LE length][JSON body]
// This is what gets sent over TCP.
Bytes serializeMessage(const Message& msg);

// Deserialize a JSON string (body only, after length prefix is stripped)
// back into a Message.
Message deserializeMessage(const std::string& jsonBody);

// =============================================================================
// Framing Helpers
// =============================================================================

// Encode a uint32 as 4 little-endian bytes
void encodeLength(uint32_t length, uint8_t* buf);

// Decode 4 little-endian bytes into a uint32
uint32_t decodeLength(const uint8_t* buf);

} // namespace net
} // namespace titancore
