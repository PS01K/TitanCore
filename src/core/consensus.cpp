// =============================================================================
// TitanCore — PoA Consensus Implementation
// =============================================================================
//
// The round-robin scheduling is trivially simple:
//   producer = authorities_[blockIndex % authorities_.size()]
//
// The complexity is NOT in the algorithm — it's in the guarantees:
//   - Every node computes the same producer for the same index
//   - A block signed by the wrong authority is rejected
//   - The authority set is immutable after initialization (for V1)
//
// WHY CHECK block.validator AND NOT block.validatorPublicKey?
//   The block's validator ADDRESS is derived from the public key
//   during block creation (address = SHA-256(pubKey)[0..20]).
//   verifyBlock() already checks that the address matches the public key.
//   So we only need to check the address against the PoA schedule.
// =============================================================================

#include "titancore/core/consensus.hpp"
#include "titancore/crypto/hash.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace titancore {
namespace core {

// =============================================================================
// Construction
// =============================================================================

PoAConsensus::PoAConsensus(std::vector<Address> authorities)
    : authorities_(std::move(authorities)) {

    if (authorities_.empty()) {
        throw std::invalid_argument(
            "PoA authority set cannot be empty — "
            "at least one authority is required to produce blocks");
    }

    spdlog::info("[PoA] Initialized with {} authorities", authorities_.size());
    for (size_t i = 0; i < authorities_.size(); ++i) {
        spdlog::info("[PoA]   Authority {}: {}", i,
                     crypto::toHex(authorities_[i]));
    }
}

// =============================================================================
// Scheduling
// =============================================================================

const Address& PoAConsensus::getProducer(uint64_t blockIndex) const {
    // The modulo operator gives us round-robin rotation:
    //   Block 0 → authorities[0]
    //   Block 1 → authorities[1]
    //   ...
    //   Block N → authorities[N % count]
    return authorities_[blockIndex % authorities_.size()];
}

// =============================================================================
// Validation
// =============================================================================

bool PoAConsensus::isAuthority(const Address& addr) const {
    return std::find(authorities_.begin(), authorities_.end(), addr)
           != authorities_.end();
}

bool PoAConsensus::validateBlock(const Block& block) const {
    lastError_.clear();

    // Who should produce this block?
    const Address& expected = getProducer(block.index);

    // Who actually produced it?
    if (block.validator != expected) {
        lastError_ = "Wrong block producer for index " +
                     std::to_string(block.index) + ": expected " +
                     crypto::toHex(expected) + ", got " +
                     crypto::toHex(block.validator);
        spdlog::warn("[PoA] {}", lastError_);
        return false;
    }

    return true;
}

const std::string& PoAConsensus::getLastError() const {
    return lastError_;
}

// =============================================================================
// Queries
// =============================================================================

size_t PoAConsensus::authorityCount() const {
    return authorities_.size();
}

const std::vector<Address>& PoAConsensus::getAuthorities() const {
    return authorities_;
}

} // namespace core
} // namespace titancore
