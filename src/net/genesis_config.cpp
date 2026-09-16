#include "titancore/net/genesis_config.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace titancore {
namespace net {

// =============================================================================
// GenesisConfig Convenience Methods
// =============================================================================

std::vector<Address> GenesisConfig::getAuthorityAddresses() const {
    std::vector<Address> addrs;
    addrs.reserve(authorities.size());
    for (const auto& auth : authorities) {
        addrs.push_back(auth.address);
    }
    return addrs;
}

core::AddressMap<uint64_t> GenesisConfig::getAllocations() const {
    core::AddressMap<uint64_t> allocs;
    for (const auto& auth : authorities) {
        allocs[auth.address] = auth.allocation;
    }
    return allocs;
}

crypto::KeyPair GenesisConfig::getGenesisValidatorKeys() const {
    crypto::KeyPair kp;
    kp.privateKey = genesisValidatorPrivateKey;
    kp.publicKey = crypto::derivePublicKey(genesisValidatorPrivateKey);
    return kp;
}

// =============================================================================
// Save / Load
// =============================================================================

void saveGenesisConfig(const GenesisConfig& config, const std::string& path) {
    // Ensure parent directories exist
    auto parentDir = std::filesystem::path(path).parent_path();
    if (!parentDir.empty()) {
        std::filesystem::create_directories(parentDir);
    }

    nlohmann::json j;
    j["genesisValidatorPrivateKey"] = crypto::toHex(config.genesisValidatorPrivateKey);

    nlohmann::json authArray = nlohmann::json::array();
    for (const auto& auth : config.authorities) {
        nlohmann::json a;
        a["address"] = crypto::toHex(auth.address);
        a["publicKey"] = crypto::toHex(auth.publicKey);
        a["allocation"] = auth.allocation;
        authArray.push_back(a);
    }
    j["authorities"] = authArray;

    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + path);
    }

    file << j.dump(2) << "\n";
}

GenesisConfig loadGenesisConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open genesis config: " + path);
    }

    nlohmann::json j;
    file >> j;

    GenesisConfig config;

    // Load genesis validator private key
    std::string genKeyHex = j.at("genesisValidatorPrivateKey").get<std::string>();
    config.genesisValidatorPrivateKey = crypto::fromHexFixed<32>(genKeyHex);

    // Load authorities
    for (const auto& a : j.at("authorities")) {
        AuthorityInfo auth;
        auth.address = crypto::fromHexFixed<20>(a.at("address").get<std::string>());
        auth.publicKey = crypto::fromHexFixed<33>(a.at("publicKey").get<std::string>());
        auth.allocation = a.at("allocation").get<uint64_t>();
        config.authorities.push_back(auth);
    }

    return config;
}

} // namespace net
} // namespace titancore
