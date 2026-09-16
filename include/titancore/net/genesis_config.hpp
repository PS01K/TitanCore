#pragma once

// =============================================================================
// TitanCore — Genesis Configuration
// =============================================================================
//
// The genesis configuration defines the initial state of the blockchain
// network. Every node must use the same genesis config to produce the
// same genesis block and start with the same state.
//
// The config file (genesis.json) contains:
//   1. The genesis validator private key (authority 0's key — needed by ALL
//      nodes to produce an identical genesis block)
//   2. The list of authorities (addresses, public keys, initial allocations)
//
// DEPLOYMENT:
//   1. Run --generate-keys to create genesis.json + individual key files
//   2. Copy genesis.json to ALL machines
//   3. Copy each authN.key to its respective machine only
//   4. Each machine uses --index N to identify itself
// =============================================================================

#include "titancore/common/types.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/core/state.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace titancore {
namespace net {

// Information about a single authority in the genesis config.
struct AuthorityInfo {
    Address   address;
    PublicKey  publicKey;
    uint64_t  allocation;
};

// The complete genesis configuration.
struct GenesisConfig {
    PrivateKey                  genesisValidatorPrivateKey;
    std::vector<AuthorityInfo>  authorities;

    // Convenience: build the authority address list for PoAConsensus.
    std::vector<Address> getAuthorityAddresses() const;

    // Convenience: build the allocation map for StateManager.
    core::AddressMap<uint64_t> getAllocations() const;

    // Convenience: get the genesis validator KeyPair.
    crypto::KeyPair getGenesisValidatorKeys() const;
};

// Save a genesis config to a JSON file.
void saveGenesisConfig(const GenesisConfig& config, const std::string& path);

// Load a genesis config from a JSON file.
// Throws std::runtime_error if the file doesn't exist or is malformed.
GenesisConfig loadGenesisConfig(const std::string& path);

} // namespace net
} // namespace titancore
