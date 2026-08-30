#pragma once

// =============================================================================
// TitanCore — Proof of Authority (PoA) Consensus
// =============================================================================
//
// WHAT IS PROOF OF AUTHORITY?
//   PoA is a consensus mechanism where a fixed set of trusted authorities
//   take turns producing blocks. Unlike Proof of Work (PoW), there's no
//   mining puzzle — authorities are known and pre-approved.
//
// WHY PoA FOR TITANCORE?
//   TitanCore is a private/permissioned blockchain with 5-6 nodes.
//   All participants are known and trusted. PoA is the natural fit:
//     - No wasted energy on mining
//     - Predictable block production
//     - Simple to implement and reason about
//     - Used by real networks: Ethereum's Clique, VeChain, PoA Network
//
// ROUND-ROBIN ROTATION:
//   The simplest PoA scheduling algorithm. Each authority takes a turn
//   in order, cycling back to the first authority after the last:
//
//     Block 0 (genesis):  authorities[0]
//     Block 1:            authorities[1]
//     Block 2:            authorities[2]
//     Block 3:            authorities[0]  ← wraps around
//     Block 4:            authorities[1]
//     ...
//
//   Formula: producer = authorities[blockIndex % authorityCount]
//
//   This is fair (equal opportunity) and deterministic (every node
//   can independently calculate who should produce any given block).
//
// AUTHORITY SET:
//   For V1, the authority set is fixed at initialization and never changes.
//   The ordering matters — it determines the round-robin schedule.
//   Every node must use the same authority list and same ordering.
//
//   In a future milestone, we could add authority set changes via
//   governance transactions (voting to add/remove authorities).
// =============================================================================

#include "titancore/common/types.hpp"
#include "titancore/core/block.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace titancore {
namespace core {

class PoAConsensus {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    // Initialize with the ordered list of authority addresses.
    //
    // The order defines the round-robin schedule:
    //   authorities[0] produces block 0, 3, 6, ...
    //   authorities[1] produces block 1, 4, 7, ...
    //   authorities[2] produces block 2, 5, 8, ...
    //
    // Throws std::invalid_argument if the list is empty.
    explicit PoAConsensus(std::vector<Address> authorities);

    // =========================================================================
    // Scheduling
    // =========================================================================

    // Get the authority address that should produce block at this index.
    //
    // Formula: authorities_[blockIndex % authorities_.size()]
    //
    // This is a pure function — given the same index, it always returns
    // the same authority, on any node. This determinism is crucial for
    // consensus: every node must independently agree on who should
    // produce each block.
    const Address& getProducer(uint64_t blockIndex) const;

    // =========================================================================
    // Validation
    // =========================================================================

    // Check if the given address is in the authority set.
    bool isAuthority(const Address& addr) const;

    // Validate that a block was produced by the correct authority.
    //
    // Checks: block.validator == getProducer(block.index)
    //
    // This is called by Blockchain::addBlock() to enforce PoA rules.
    // A block with a valid signature but the WRONG validator is rejected.
    bool validateBlock(const Block& block) const;

    // Return the reason for the last failed validateBlock() call.
    const std::string& getLastError() const;

    // =========================================================================
    // Queries
    // =========================================================================

    size_t authorityCount() const;
    const std::vector<Address>& getAuthorities() const;

private:
    // Ordered list of authority addresses. The ordering is the schedule.
    std::vector<Address> authorities_;

    // Human-readable error from the last failed validation.
    mutable std::string lastError_;
};

} // namespace core
} // namespace titancore
