#include "titancore/crypto/key_io.hpp"
#include "titancore/crypto/hash.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace titancore {
namespace crypto {

void savePrivateKey(const PrivateKey& key, const std::string& path) {
    // Ensure parent directories exist
    auto parentDir = std::filesystem::path(path).parent_path();
    if (!parentDir.empty()) {
        std::filesystem::create_directories(parentDir);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + path);
    }

    file << toHex(key) << "\n";
}

PrivateKey loadPrivateKey(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open key file: " + path);
    }

    std::string hexLine;
    std::getline(file, hexLine);

    // Trim whitespace
    hexLine.erase(
        std::remove_if(hexLine.begin(), hexLine.end(), ::isspace),
        hexLine.end());

    if (hexLine.empty()) {
        throw std::runtime_error("Key file is empty: " + path);
    }

    return fromHexFixed<32>(hexLine);
}

} // namespace crypto
} // namespace titancore
