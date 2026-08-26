#pragma once

// =============================================================================
// TitanCore — Persistent Storage (LevelDB)
// =============================================================================
//
// This module provides persistent storage for the blockchain using LevelDB,
// a fast key-value store created by Google engineers Jeff Dean and Sanjay
// Ghemawat — the same people behind MapReduce and BigTable.
//
// WHY LEVELDB?
//   - Originally used by Bitcoin Core for block/transaction indexing
//   - Simple API: Put(key, value), Get(key), Delete(key)
//   - Sorted keys enable efficient range scans (e.g., "all blocks")
//   - Embedded library (no separate server process)
//   - Excellent write throughput via LSM-tree architecture
//
// KEY-PREFIX NAMESPACING:
//   LevelDB is a flat key-value space — no tables, no schemas. To organize
//   different types of data, we use key prefixes:
//
//     "b:00000001"        → Block at index 1 (serialized as JSON string)
//     "b:00000002"        → Block at index 2
//     "h:a1b2c3d4e5..."   → Hash→index mapping (block hash → block index)
//     "a:e5f6a7b8c9..."   → Account state (address → balance + nonce as JSON)
//     "m:height"          → Chain metadata (current height as string)
//
//   The block index is zero-padded to 8 digits so that lexicographic
//   sorting (how LevelDB sorts keys) matches numeric sorting:
//     "b:00000001" < "b:00000002" < "b:00000010"
//   Without padding: "b:10" < "b:2" (wrong! 10 > 2 numerically)
//
// RAII (Resource Acquisition Is Initialization):
//   LevelDB's C API returns a raw pointer (leveldb::DB*) that you must
//   manually delete when done. We wrap it in std::unique_ptr so the
//   database is automatically closed when the Storage object is destroyed
//   (goes out of scope, is deleted, etc.). This prevents resource leaks.
//
// DESIGN: SEPARATE PERSISTENCE LAYER
//   Storage is intentionally separate from Blockchain and StateManager.
//   It doesn't know about chain rules or economic rules — it just stores
//   and retrieves data. The application code coordinates between them:
//
//     On startup:   Load blocks from Storage → replay through Blockchain
//     After mining:  Save new block to Storage, save updated state
//
//   This separation means we could swap LevelDB for RocksDB (or SQLite,
//   or even a remote database) without touching any business logic.
// =============================================================================

#include "titancore/common/types.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/state.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Forward declaration — avoids including the entire LevelDB header here.
// Consumers of Storage don't need to know about LevelDB's API.
namespace leveldb {
    class DB;
}

namespace titancore {
namespace storage {

class Storage {
public:
    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    // Open (or create) a LevelDB database at the given filesystem path.
    //
    // If the directory doesn't exist, LevelDB creates it.
    // If the directory already contains a database, it's opened.
    //
    // Throws std::runtime_error if the database cannot be opened
    // (e.g., permissions error, corrupted data).
    explicit Storage(const std::string& dbPath);

    // The destructor closes the database. Because we use std::unique_ptr
    // for the leveldb::DB*, this happens automatically — no manual
    // cleanup needed. This is RAII in action.
    ~Storage();

    // Non-copyable (database handles shouldn't be shared)
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    // Movable (transfer ownership)
    Storage(Storage&&) noexcept;
    Storage& operator=(Storage&&) noexcept;

    // =========================================================================
    // Block Operations
    // =========================================================================

    // Save a block to storage.
    //
    // Stores two entries:
    //   1. "b:<zero-padded index>" → block JSON
    //   2. "h:<hex hash>" → block index (for hash-based lookups)
    //
    // Throws std::runtime_error on write failure.
    void saveBlock(const core::Block& block);

    // Load a block by its index.
    //
    // Returns std::nullopt if the block doesn't exist.
    // Throws std::runtime_error on read/parse failure.
    std::optional<core::Block> loadBlock(uint64_t index);

    // Load a block by its hash.
    //
    // Uses the hash→index mapping, then loads the block by index.
    // Returns std::nullopt if not found.
    std::optional<core::Block> loadBlockByHash(const Hash& hash);

    // =========================================================================
    // State Operations
    // =========================================================================

    // Save an account's state (balance + nonce).
    //
    // Key: "a:<hex address>"
    // Value: JSON {"balance": 10000, "nonce": 5}
    void saveAccountState(const Address& addr,
                          const core::AccountState& acct);

    // Load an account's state.
    //
    // Returns std::nullopt if the account doesn't exist in storage.
    std::optional<core::AccountState> loadAccountState(
        const Address& addr);

    // Load all stored accounts.
    //
    // Uses LevelDB's iterator to scan all keys with the "a:" prefix.
    // Returns a vector of (address, account state) pairs.
    std::vector<std::pair<Address, core::AccountState>> loadAllAccounts();

    // =========================================================================
    // Metadata Operations
    // =========================================================================

    // Save the current chain height.
    // This tells us how many blocks to load on startup.
    void saveChainHeight(uint64_t height);

    // Load the chain height.
    // Returns 0 if no height has been saved (fresh database).
    uint64_t loadChainHeight();

    // =========================================================================
    // Utility
    // =========================================================================

    // Delete all data in the database.
    // Closes the DB, destroys all files, and reopens.
    // Used primarily in tests for clean state between test cases.
    void clear();

private:
    // The LevelDB database handle, wrapped in unique_ptr for RAII.
    //
    // WHY unique_ptr?
    //   leveldb::DB::Open() returns a raw pointer (leveldb::DB*).
    //   If we stored it as a raw pointer, we'd need to manually call
    //   delete in the destructor, and we'd risk leaking it if an
    //   exception is thrown before the destructor runs.
    //
    //   unique_ptr automatically calls delete when it goes out of scope,
    //   even if an exception is thrown. This is the C++ idiom for
    //   owning a resource (RAII).
    std::unique_ptr<leveldb::DB> db_;

    // Remember the path so we can reopen after clear().
    std::string dbPath_;

    // Helper: format a block index as a zero-padded 8-digit string.
    // 1 → "00000001", 42 → "00000042"
    static std::string padIndex(uint64_t index);
};

} // namespace storage
} // namespace titancore
