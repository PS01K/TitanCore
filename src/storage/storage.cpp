// =============================================================================
// TitanCore — Persistent Storage Implementation
// =============================================================================
//
// This file implements the Storage class using LevelDB.
//
// LEVELDB API OVERVIEW:
//   LevelDB has a very simple API:
//
//     Status Put(WriteOptions, key, value)   — write a key-value pair
//     Status Get(ReadOptions, key, &value)   — read a value by key
//     Status Delete(WriteOptions, key)       — remove a key-value pair
//     Iterator* NewIterator(ReadOptions)     — iterate over key-value pairs
//
//   Every operation returns a Status object that tells you if it succeeded.
//   We check every Status and throw on failure — silent data loss is
//   unacceptable for a blockchain.
//
// SERIALIZATION STRATEGY:
//   - Blocks: serialized to JSON via our existing toJson()/blockFromJson()
//   - Account state: serialized as JSON {"balance": 10000, "nonce": 5}
//   - Metadata: stored as plain strings ("42" for chain height)
//
//   We use JSON because it's human-readable (great for debugging),
//   consistent with how we already serialize blocks, and the performance
//   overhead is negligible for V1. Production systems might use Protocol
//   Buffers or a binary format for smaller storage footprint.
//
// ERROR HANDLING:
//   Storage operations throw std::runtime_error on failure. This is
//   appropriate because storage failures are fatal — if we can't read
//   or write data, the node cannot function. The caller should catch
//   these at the application level and shut down gracefully.
// =============================================================================

#include "titancore/storage/storage.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/crypto/hash.hpp"

#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>

namespace titancore {
namespace storage {

// Key prefixes — constants for the namespacing scheme
static const std::string BLOCK_PREFIX = "b:";
static const std::string HASH_PREFIX  = "h:";
static const std::string ACCT_PREFIX  = "a:";
static const std::string META_PREFIX  = "m:";

// =============================================================================
// Construction / Destruction
// =============================================================================

Storage::Storage(const std::string& dbPath)
    : dbPath_(dbPath) {
    // LevelDB options:
    //   create_if_missing: create the database if it doesn't exist yet
    //   (without this, opening a nonexistent DB would fail)
    leveldb::Options options;
    options.create_if_missing = true;

    // leveldb::DB::Open returns a raw pointer. We'll wrap it in unique_ptr.
    leveldb::DB* rawDb = nullptr;
    leveldb::Status status = leveldb::DB::Open(options, dbPath, &rawDb);

    if (!status.ok()) {
        throw std::runtime_error("Failed to open LevelDB at '" + dbPath +
                                 "': " + status.ToString());
    }

    // Transfer ownership to unique_ptr. When db_ is destroyed (or reset),
    // it will automatically call `delete rawDb`.
    db_.reset(rawDb);

    spdlog::info("[Storage] Opened database at: {}", dbPath);
}

// Default destructor — unique_ptr handles cleanup
Storage::~Storage() = default;

// Move constructor
Storage::Storage(Storage&&) noexcept = default;

// Move assignment
Storage& Storage::operator=(Storage&&) noexcept = default;

// =============================================================================
// Helpers
// =============================================================================

std::string Storage::padIndex(uint64_t index) {
    // Zero-pad to 8 digits so lexicographic sort matches numeric sort.
    // 1 → "00000001", 42 → "00000042", 12345678 → "12345678"
    //
    // 8 digits supports up to 99,999,999 blocks. For V1 this is plenty.
    // If we ever need more, we'd extend the padding.
    std::string s = std::to_string(index);
    if (s.length() < 8) {
        s.insert(0, 8 - s.length(), '0');
    }
    return s;
}

// =============================================================================
// Block Operations
// =============================================================================

void Storage::saveBlock(const core::Block& block) {
    // Serialize the block to JSON string
    std::string blockJson = core::toJson(block).dump();

    // Key 1: block by index — "b:00000001" → block JSON
    std::string indexKey = BLOCK_PREFIX + padIndex(block.index);

    // Key 2: hash→index mapping — "h:abcdef..." → "1"
    std::string hashKey = HASH_PREFIX + crypto::toHex(block.hash);
    std::string indexStr = std::to_string(block.index);

    // Use a WriteBatch to write both atomically.
    //
    // WriteBatch ensures that either BOTH writes succeed or NEITHER does.
    // Without this, a crash between the two writes could leave us with
    // a block stored but no hash mapping (or vice versa).
    leveldb::WriteBatch batch;
    batch.Put(indexKey, blockJson);
    batch.Put(hashKey, indexStr);

    leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
    if (!status.ok()) {
        throw std::runtime_error("Failed to save block " +
                                 std::to_string(block.index) + ": " +
                                 status.ToString());
    }

    spdlog::debug("[Storage] Saved block {}: {}",
                  block.index, crypto::toHex(block.hash));
}

std::optional<core::Block> Storage::loadBlock(uint64_t index) {
    std::string key = BLOCK_PREFIX + padIndex(index);
    std::string value;

    leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        return std::nullopt;  // Block doesn't exist — not an error
    }

    if (!status.ok()) {
        throw std::runtime_error("Failed to load block " +
                                 std::to_string(index) + ": " +
                                 status.ToString());
    }

    // Parse the JSON string back into a Block
    nlohmann::json j = nlohmann::json::parse(value);
    return core::blockFromJson(j);
}

std::optional<core::Block> Storage::loadBlockByHash(const Hash& hash) {
    // Step 1: Look up the hash→index mapping
    std::string hashKey = HASH_PREFIX + crypto::toHex(hash);
    std::string indexStr;

    leveldb::Status status = db_->Get(leveldb::ReadOptions(), hashKey, &indexStr);

    if (status.IsNotFound()) {
        return std::nullopt;
    }

    if (!status.ok()) {
        throw std::runtime_error("Failed to look up block hash: " +
                                 status.ToString());
    }

    // Step 2: Load the block by its index
    uint64_t index = std::stoull(indexStr);
    return loadBlock(index);
}

// =============================================================================
// State Operations
// =============================================================================

void Storage::saveAccountState(const Address& addr,
                               const core::AccountState& acct) {
    std::string key = ACCT_PREFIX + crypto::toHex(addr);

    // Serialize as a simple JSON object
    nlohmann::json j;
    j["balance"] = acct.balance;
    j["nonce"] = acct.nonce;
    std::string value = j.dump();

    leveldb::Status status = db_->Put(leveldb::WriteOptions(), key, value);
    if (!status.ok()) {
        throw std::runtime_error("Failed to save account state: " +
                                 status.ToString());
    }
}

std::optional<core::AccountState> Storage::loadAccountState(
    const Address& addr) {

    std::string key = ACCT_PREFIX + crypto::toHex(addr);
    std::string value;

    leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        return std::nullopt;
    }

    if (!status.ok()) {
        throw std::runtime_error("Failed to load account state: " +
                                 status.ToString());
    }

    nlohmann::json j = nlohmann::json::parse(value);
    core::AccountState acct;
    acct.balance = j["balance"].get<uint64_t>();
    acct.nonce = j["nonce"].get<uint64_t>();
    return acct;
}

std::vector<std::pair<Address, core::AccountState>>
Storage::loadAllAccounts() {
    std::vector<std::pair<Address, core::AccountState>> accounts;

    // LevelDB iterators let you scan keys in sorted order.
    // We Seek() to the first key with the "a:" prefix, then iterate
    // until we hit a key that doesn't start with "a:".
    std::unique_ptr<leveldb::Iterator> it(
        db_->NewIterator(leveldb::ReadOptions()));

    for (it->Seek(ACCT_PREFIX); it->Valid(); it->Next()) {
        std::string key = it->key().ToString();

        // Stop when we've passed the account prefix
        if (key.substr(0, ACCT_PREFIX.size()) != ACCT_PREFIX) {
            break;
        }

        // Extract address from key: "a:e5f6a7b8..." → "e5f6a7b8..."
        std::string addrHex = key.substr(ACCT_PREFIX.size());
        Bytes addrBytes = crypto::fromHex(addrHex);

        // Convert Bytes (vector) to Address (std::array<uint8_t, 20>)
        Address addr{};
        if (addrBytes.size() >= addr.size()) {
            std::copy_n(addrBytes.begin(), addr.size(), addr.begin());
        }

        // Parse account state from value
        nlohmann::json j = nlohmann::json::parse(it->value().ToString());
        core::AccountState acct;
        acct.balance = j["balance"].get<uint64_t>();
        acct.nonce = j["nonce"].get<uint64_t>();

        accounts.emplace_back(addr, acct);
    }

    return accounts;
}

// =============================================================================
// Metadata Operations
// =============================================================================

void Storage::saveChainHeight(uint64_t height) {
    std::string key = META_PREFIX + "height";
    std::string value = std::to_string(height);

    leveldb::Status status = db_->Put(leveldb::WriteOptions(), key, value);
    if (!status.ok()) {
        throw std::runtime_error("Failed to save chain height: " +
                                 status.ToString());
    }
}

uint64_t Storage::loadChainHeight() {
    std::string key = META_PREFIX + "height";
    std::string value;

    leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        return 0;  // Fresh database — no chain yet
    }

    if (!status.ok()) {
        throw std::runtime_error("Failed to load chain height: " +
                                 status.ToString());
    }

    return std::stoull(value);
}

// =============================================================================
// Utility
// =============================================================================

void Storage::clear() {
    // Close the database first
    db_.reset();

    // Destroy all LevelDB files at the path
    leveldb::Status status = leveldb::DestroyDB(dbPath_, leveldb::Options());
    if (!status.ok()) {
        throw std::runtime_error("Failed to destroy database: " +
                                 status.ToString());
    }

    // Reopen a fresh, empty database
    leveldb::Options options;
    options.create_if_missing = true;

    leveldb::DB* rawDb = nullptr;
    status = leveldb::DB::Open(options, dbPath_, &rawDb);
    if (!status.ok()) {
        throw std::runtime_error("Failed to reopen database after clear: " +
                                 status.ToString());
    }

    db_.reset(rawDb);
    spdlog::info("[Storage] Database cleared and reopened");
}

} // namespace storage
} // namespace titancore
