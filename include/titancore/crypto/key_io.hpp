#pragma once

// =============================================================================
// TitanCore — Key File I/O
// =============================================================================
//
// Save and load private keys to/from files. Keys are stored as hex-encoded
// strings, one per line. This keeps key files human-readable and easy to
// verify without specialized tools.
//
// File format:
//   - Single line containing the hex-encoded private key (64 hex chars = 32 bytes)
//   - No headers, no JSON, no binary framing
//
// Example file contents:
//   4a2f8c1d9e3b7a6f5c0d8e2b1a4f7c3d9e6b5a0f8c2d1e7b4a3f6c9d0e5b8a
//
// SECURITY NOTE:
//   Private key files should never be committed to version control.
//   Add *.key to .gitignore.
// =============================================================================

#include "titancore/common/types.hpp"

#include <string>

namespace titancore {
namespace crypto {

// Save a private key to a file as a hex string.
// Creates the file (and parent directories) if they don't exist.
// Overwrites the file if it already exists.
void savePrivateKey(const PrivateKey& key, const std::string& path);

// Load a private key from a hex string file.
// Throws std::runtime_error if the file doesn't exist or is malformed.
PrivateKey loadPrivateKey(const std::string& path);

} // namespace crypto
} // namespace titancore
