// =============================================================================
// TitanCore — SHA-256 Hashing & Hex Utilities (Implementation)
// =============================================================================
//
// This file implements SHA-256 hashing using OpenSSL's EVP (Envelope) API.
//
// WHY THE EVP API?
//   OpenSSL has two ways to do hashing:
//     1. Legacy API: SHA256() — a simple one-shot function (deprecated)
//     2. EVP API: EVP_DigestInit/Update/Final — the modern, recommended way
//
//   The EVP API is "algorithm-agnostic" — the same code pattern works for
//   SHA-256, SHA-512, SHA-3, etc. You just swap out EVP_sha256() for the
//   algorithm you want. This makes it future-proof.
//
//   The EVP workflow is always:
//     1. Create a context (EVP_MD_CTX_new)
//     2. Initialize it with an algorithm (EVP_DigestInit_ex)
//     3. Feed data in one or more chunks (EVP_DigestUpdate)
//     4. Finalize and get the hash output (EVP_DigestFinal_ex)
//     5. Clean up the context (EVP_MD_CTX_free)
//
// RAII NOTE:
//   The EVP context must be freed to avoid memory leaks. We use a
//   std::unique_ptr with a custom deleter to ensure cleanup happens
//   automatically, even if an exception is thrown. This pattern is called
//   RAII (Resource Acquisition Is Initialization) — a core C++ idiom.
// =============================================================================

#include "titancore/crypto/hash.hpp"

#include <openssl/evp.h>
#include <memory>
#include <stdexcept>

namespace titancore {
namespace crypto {

// --- SHA-256 Implementation --------------------------------------------------

Hash sha256(const Bytes& data) {
    // Step 1: Create the digest context.
    // EVP_MD_CTX_new() allocates a new context on the heap.
    // We wrap it in a unique_ptr with EVP_MD_CTX_free as the custom deleter,
    // so it's automatically freed when 'ctx' goes out of scope (RAII).
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(
        EVP_MD_CTX_new(), EVP_MD_CTX_free
    );

    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    // Step 2: Initialize the context for SHA-256.
    // EVP_sha256() returns a pointer to the SHA-256 algorithm descriptor.
    // The nullptr argument is for the "engine" — we use the default.
    if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    // Step 3: Feed the data into the hash function.
    // You can call EVP_DigestUpdate multiple times to hash data in chunks.
    // This is useful when hashing large files or streaming data.
    // For us, we hash the entire buffer at once.
    if (EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1) {
        throw std::runtime_error("EVP_DigestUpdate failed");
    }

    // Step 4: Finalize — compute the hash and write it to our output array.
    Hash result{};
    unsigned int length = 0;

    if (EVP_DigestFinal_ex(ctx.get(), result.data(), &length) != 1) {
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    // Sanity check: SHA-256 should always produce 32 bytes
    if (length != 32) {
        throw std::runtime_error("SHA-256 produced unexpected length");
    }

    return result;
    // Step 5: ctx is automatically freed here by the unique_ptr destructor
}

Hash sha256(const std::string& data) {
    // Convert string to Bytes (vector<uint8_t>) and delegate.
    // reinterpret_cast is needed because std::string uses 'char' internally,
    // but our Bytes type uses 'uint8_t'. They're the same size (1 byte),
    // this cast just tells the compiler "treat these chars as unsigned bytes."
    Bytes bytes(
        reinterpret_cast<const uint8_t*>(data.data()),
        reinterpret_cast<const uint8_t*>(data.data()) + data.size()
    );
    return sha256(bytes);
}

Hash doubleSha256(const Bytes& data) {
    // First hash
    Hash firstHash = sha256(data);

    // Hash the hash — convert the fixed-size array to a Bytes vector
    Bytes hashBytes(firstHash.begin(), firstHash.end());
    return sha256(hashBytes);
}

// --- Hex Encoding/Decoding ---------------------------------------------------

std::string toHex(const Bytes& data) {
    std::string hex;
    hex.reserve(data.size() * 2);

    static constexpr char hexChars[] = "0123456789abcdef";
    for (uint8_t byte : data) {
        hex.push_back(hexChars[(byte >> 4) & 0x0F]);
        hex.push_back(hexChars[byte & 0x0F]);
    }

    return hex;
}

Bytes fromHex(const std::string& hex) {
    // Hex strings must have an even number of characters
    // because each byte is represented by exactly 2 hex chars.
    if (hex.size() % 2 != 0) {
        return {};  // Return empty on invalid input
    }

    Bytes result;
    result.reserve(hex.size() / 2);

    for (size_t i = 0; i < hex.size(); i += 2) {
        // Convert each pair of hex characters to a byte.
        //
        // std::stoul("a1", nullptr, 16) parses "a1" as base-16 → 161
        // We parse 2 characters at a time using substr(i, 2).
        //
        // We use a try-catch because stoul throws if the input
        // contains non-hex characters.
        try {
            uint8_t byte = static_cast<uint8_t>(
                std::stoul(hex.substr(i, 2), nullptr, 16)
            );
            result.push_back(byte);
        } catch (...) {
            return {};  // Return empty on invalid hex characters
        }
    }

    return result;
}

} // namespace crypto
} // namespace titancore
