# TitanCore

A permissioned blockchain with a native cryptocurrency called **ODM**, built from scratch in C++ for educational purposes.

## What is TitanCore?

TitanCore is a learning project that implements a complete blockchain system — from cryptographic primitives to peer-to-peer networking. It is **not** a fork of Bitcoin or Ethereum; every component is built incrementally to understand how blockchain infrastructure works internally.

## V1 Scope

TitanCore V1 is a **private/permissioned** blockchain designed to run across 5–6 trusted authority nodes using **Proof of Authority (PoA)** consensus.

### Features (planned for V1)

| Feature | Description |
|---|---|
| Cryptographic Keys | secp256k1 key pairs and address derivation |
| Digital Signatures | ECDSA signing and verification |
| Transactions | ODM transfers with nonce-based replay protection |
| Blocks | Block creation, hashing, and chain linking |
| Genesis Block | Initial block with ODM allocations |
| Blockchain Validation | Full chain integrity verification |
| State Management | Account-based balance tracking |
| Native ODM Currency | Built-in to the protocol (not a token) |
| Persistent Storage | LevelDB for blocks and state |
| P2P Networking | TCP-based node communication |
| Transaction Propagation | Gossip-based transaction broadcast |
| Block Propagation | Gossip-based block broadcast |
| PoA Consensus | Round-robin authority block creation |
| Multi-Node Sync | Chain synchronization across nodes |
| RPC Interface | JSON-RPC for external interaction |

### Explicitly Out of Scope for V1

Smart contracts, EVM, Solidity, ERC tokens, mining, staking, governance, bridges, Layer 2, NFTs, public deployment.

## Technology Stack

| Component | Technology | Why |
|---|---|---|
| Language | C++17 | Performance, memory control, industry standard for blockchains |
| Build System | CMake 3.20+ | De facto standard for C++ projects |
| Cryptography | OpenSSL + libsecp256k1 | SHA-256 hashing + Bitcoin's elliptic curve library |
| Serialization | nlohmann/json | Developer-friendly JSON library |
| Logging | spdlog | Fast, structured logging with log levels |
| Networking | Boost.Asio | Industry-standard async I/O |
| Storage | LevelDB | Simple, fast key-value store (used by Bitcoin) |
| Testing | GoogleTest | Industry-standard C++ testing framework |

## Building

### Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, or MSVC 2017+)
- CMake 3.20 or later
- Git (for FetchContent to download dependencies)

### Build Steps

```bash
# Configure the project (downloads dependencies on first run)
mkdir -p build && cd build
cmake ..

# Build
cmake --build .

# Run the node
./titancore_node

# Run tests
ctest --output-on-failure
```

## Project Structure

```
TitanCore/
├── CMakeLists.txt              # Root build configuration
├── README.md                   # This file
├── .gitignore
├── cmake/
│   └── Dependencies.cmake      # External dependency declarations
├── include/titancore/           # Public headers (by module)
│   ├── common/                 # Shared types, utilities, version
│   ├── crypto/                 # Keys, hashing, signatures
│   ├── core/                   # Transaction, Block structures
│   ├── blockchain/             # Chain management
│   ├── state/                  # Account state
│   ├── consensus/              # PoA logic
│   ├── network/                # P2P networking
│   ├── storage/                # Persistent storage
│   └── rpc/                    # JSON-RPC interface
├── src/                        # Implementation files
│   └── main.cpp                # Node entry point
└── tests/                      # Unit tests
    └── test_main.cpp
```

## Native Currency: ODM

ODM is TitanCore's native cryptocurrency. It is built directly into the blockchain protocol — it is **not** an ERC-20 token and TitanCore does **not** use Ethereum.

- **Name:** ODM
- **Symbol:** ODM
- **Type:** Native protocol currency (like ETH is to Ethereum, or BTC is to Bitcoin)

## License

This is an educational project.
