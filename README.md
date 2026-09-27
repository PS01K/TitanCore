# TitanCore

A permissioned blockchain with a native cryptocurrency called **ODM**, built from scratch in C++.

## What is TitanCore?

TitanCore is a complete blockchain system built from the ground up — from cryptographic primitives to peer-to-peer networking, persistent storage, and a JSON-RPC interface. It is **not** a fork of Bitcoin or Ethereum; every component was implemented incrementally.

**~10,000 lines of C++ · 262 automated tests · Tested on LAN across Mac and Windows**

## Features

| Feature | Description |
|---|---|
| Cryptographic Keys | secp256k1 key pairs with address derivation |
| Digital Signatures | ECDSA signing and verification |
| Transactions | ODM transfers with nonce-based replay protection |
| Blocks | Block creation, hashing, and chain linking |
| Genesis Block | Configurable initial block with ODM allocations |
| Blockchain Validation | Full chain integrity verification |
| State Management | Account-based balance and nonce tracking |
| Native ODM Currency | Built into the protocol (not a token) |
| Persistent Storage | LevelDB for blocks and state (survives restarts) |
| P2P Networking | TCP-based node communication with mesh topology |
| Transaction Propagation | Gossip-based transaction broadcast across peers |
| Block Propagation | Gossip-based block broadcast with chain sync |
| PoA Consensus | Round-robin authority block production |
| Multi-Machine Networking | CLI-driven nodes with static peer configuration |
| Offline Transaction Signing | `--sign-tx` mode for secure offline signing |
| JSON-RPC Interface | HTTP API for external wallets, scripts, and dashboards |
| E2E Demo | Automated script proving the full lifecycle |

### Explicitly Out of Scope

Smart contracts, EVM, Solidity, ERC tokens, mining, staking, governance, bridges, Layer 2, NFTs, public deployment.

## Technology Stack

| Component | Technology | Why |
|---|---|---|
| Language | C++17 | Performance, memory control, industry standard for blockchains |
| Build System | CMake 3.20+ | De facto standard for C++ projects |
| Hashing | OpenSSL (libcrypto) | SHA-256 via battle-tested library |
| Elliptic Curves | libsecp256k1 | Bitcoin Core's library for key generation and ECDSA |
| Serialization | nlohmann/json | Developer-friendly JSON library |
| Logging | spdlog | Fast, structured logging with log levels |
| Networking | Standalone ASIO | Industry-standard async I/O (no Boost dependency) |
| Storage | LevelDB | Fast key-value store (created at Google, used by Bitcoin) |
| HTTP Server | cpp-httplib | Header-only HTTP server for JSON-RPC |
| Testing | GoogleTest | Industry-standard C++ testing framework |

All dependencies except OpenSSL are automatically downloaded via CMake FetchContent.

## Building

### Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, or MSVC 2017+)
- CMake 3.20 or later
- OpenSSL development libraries
- Git (for FetchContent to download dependencies)

**macOS (Homebrew):**
```bash
brew install cmake openssl
```

### Build Steps

```bash
# Configure (downloads dependencies on first run)
mkdir -p build && cd build
cmake ..

# Build
cmake --build .

# Run tests (262 tests)
ctest --output-on-failure
```

## Usage

TitanCore has four modes of operation:

### 1. Bootstrap a Network

Generate authority keys and a genesis configuration:

```bash
./titancore_node --generate-keys --count 3 --genesis-dir genesis/
```

This creates:
- `genesis/genesis.json` — shared network configuration
- `genesis/auth0.key`, `auth1.key`, `auth2.key` — private keys (one per node)

### 2. Run a Node

```bash
./titancore_node --index 0 --port 9001 --rpc-port 8545 \
    --peers 192.168.1.5:9002,192.168.1.6:9003 \
    --data-dir data/node0 --genesis-dir genesis/
```

| Option | Description | Default |
|---|---|---|
| `--index N` | Authority index (0, 1, 2, ...) | required |
| `--port P` | P2P listen port | 9000 |
| `--rpc-port P` | JSON-RPC HTTP port | 8545 |
| `--peers H:P,...` | Comma-separated peer addresses | none |
| `--data-dir DIR` | LevelDB data directory | `data/nodeN` |
| `--genesis-dir DIR` | Genesis config directory | `genesis/` |

### 3. Sign a Transaction (Offline)

Create a signed transaction without running a node:

```bash
./titancore_node --sign-tx --key genesis/auth0.key \
    --to a1b2c3d4... --amount 500 --nonce 0
```

Outputs clean JSON to stdout, ready to pipe into curl:

```bash
TX=$(./titancore_node --sign-tx --key genesis/auth0.key --to a1b2... --amount 500 --nonce 0)
curl -X POST http://localhost:8545/rpc \
    -H 'Content-Type: application/json' \
    -d "{\"jsonrpc\":\"2.0\",\"method\":\"submitTransaction\",\"params\":{\"transaction\":${TX}},\"id\":1}"
```

### 4. JSON-RPC Interface

All RPC calls use `POST /rpc` with JSON-RPC 2.0 format:

```bash
curl -X POST http://localhost:8545/rpc \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","method":"getBalance","params":{"address":"a1b2..."},"id":1}'
```

| Method | Params | Returns |
|---|---|---|
| `getBalance` | `{address}` | `{balance}` |
| `getNonce` | `{address}` | `{nonce}` |
| `getChainHeight` | none | `{height}` |
| `getBlockByIndex` | `{index}` | `{block: {...}}` |
| `getMempoolSize` | none | `{size}` |
| `getPeerCount` | none | `{count}` |
| `submitTransaction` | `{transaction: {...}}` | `{accepted, hash}` |

### 5. Run the E2E Demo

A self-contained script that proves the entire V1 lifecycle:

```bash
./scripts/demo_e2e.sh
```

This bootstraps 3 nodes, submits transactions via RPC, verifies balances across all nodes, and tests persistence by killing and restarting a node.

## Multi-Machine Deployment

To run nodes on separate physical machines over LAN:

1. **Bootstrap** on one machine: `./titancore_node --generate-keys --count 3`
2. **Distribute** `genesis/genesis.json` to all machines
3. **Each machine** keeps only its own `auth<N>.key`
4. **Start each node** with `--peers` pointing to the other machines' LAN IPs

```bash
# Machine A (192.168.1.10)
./titancore_node --index 0 --port 9001 --peers 192.168.1.11:9001,192.168.1.12:9001

# Machine B (192.168.1.11)
./titancore_node --index 1 --port 9001 --peers 192.168.1.10:9001

# Machine C (192.168.1.12)
./titancore_node --index 2 --port 9001 --peers 192.168.1.10:9001
```

## Project Structure

```
TitanCore/
├── CMakeLists.txt                    # Root build configuration
├── cmake/
│   └── Dependencies.cmake            # FetchContent dependency declarations
├── include/titancore/                 # Public headers (by module)
│   ├── common/
│   │   ├── types.hpp                  # Address, Hash, PublicKey, Signature types
│   │   └── version.hpp                # Version constants
│   ├── crypto/
│   │   ├── hash.hpp                   # SHA-256, hex encoding
│   │   ├── keys.hpp                   # Key generation, ECDSA sign/verify
│   │   └── key_io.hpp                 # Key file I/O (load/save hex)
│   ├── core/
│   │   ├── transaction.hpp            # Transaction structure, JSON serialization
│   │   ├── block.hpp                  # Block structure, hashing, validation
│   │   ├── blockchain.hpp             # Chain management, genesis block
│   │   ├── state.hpp                  # Account balances and nonces
│   │   ├── mempool.hpp                # Pending transaction pool
│   │   └── consensus.hpp              # PoA round-robin authority rotation
│   ├── storage/
│   │   └── storage.hpp                # LevelDB persistence layer
│   ├── net/
│   │   ├── node.hpp                   # Node orchestrator (ties everything together)
│   │   ├── network_manager.hpp        # TCP listener and peer management
│   │   ├── peer_session.hpp           # Individual peer connection handler
│   │   ├── message.hpp                # Binary message framing protocol
│   │   └── genesis_config.hpp         # Genesis JSON config serialization
│   └── rpc/
│       └── rpc_server.hpp             # HTTP JSON-RPC 2.0 server
├── src/                               # Implementation files (mirrors include/)
│   ├── main.cpp                       # Entry point (4 modes)
│   ├── crypto/                        # hash.cpp, keys.cpp, key_io.cpp
│   ├── core/                          # transaction.cpp, block.cpp, etc.
│   ├── storage/                       # storage.cpp
│   ├── net/                           # node.cpp, network_manager.cpp, etc.
│   └── rpc/                           # rpc_server.cpp
├── tests/                             # Unit tests (262 total)
│   ├── test_main.cpp                  # Version and setup tests
│   ├── crypto/                        # test_hash.cpp, test_keys.cpp
│   ├── core/                          # test_transaction.cpp, test_block.cpp, etc.
│   ├── storage/                       # test_storage.cpp
│   ├── net/                           # test_message.cpp, test_node.cpp
│   └── rpc/                           # test_rpc_server.cpp
└── scripts/
    └── demo_e2e.sh                    # End-to-end lifecycle demo
```

## Architecture

```
                          ┌─────────────────────────────────────┐
  External Clients ──────▶│           RPC Server                │
  (curl, wallets)         │         (cpp-httplib)               │
                          └────────────────┬────────────────────┘
                                           │
                          ┌────────────────▼────────────────────┐
                          │              Node                   │
                          │         (orchestrator)              │
                          └──┬──────┬──────┬──────┬──────┬─────┘
                             │      │      │      │      │
                      ┌──────▼─┐ ┌──▼───┐ ┌▼────┐ ┌▼───┐ ┌▼───────┐
                      │Mempool │ │State │ │Chain│ │PoA │ │Storage  │
                      │        │ │      │ │     │ │    │ │(LevelDB)│
                      └────────┘ └──────┘ └─────┘ └────┘ └─────────┘
                             │
                      ┌──────▼─────────────────────────────────┐
                      │         Network Manager                │
                      │     (TCP listener + peer mesh)         │
                      └──────┬──────────────────┬──────────────┘
                             │                  │
                      ┌──────▼──────┐    ┌──────▼──────┐
                      │ PeerSession │    │ PeerSession │  ...
                      │  (node 1)   │    │  (node 2)   │
                      └─────────────┘    └─────────────┘
```

## Native Currency: ODM

ODM is TitanCore's native cryptocurrency. It is built directly into the blockchain protocol — it is **not** an ERC-20 token and TitanCore does **not** use Ethereum.

- **Name:** ODM
- **Symbol:** ODM
- **Type:** Native protocol currency (like ETH is to Ethereum, or BTC is to Bitcoin)
- **Genesis Allocation:** 10,000 ODM per authority (configurable)

## License

This is an educational project.
