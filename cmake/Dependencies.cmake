# ==============================================================================
# TitanCore — External Dependencies
# ==============================================================================
# This file uses CMake's FetchContent to download and build dependencies.
#
# FetchContent works by:
#   1. FetchContent_Declare() — tells CMake WHERE to get the dependency
#   2. FetchContent_MakeAvailable() — downloads it (if not cached) and makes
#      its targets available to our project
#
# Dependencies are cached in the build/_deps/ directory, so they are only
# downloaded once (unless you delete the build directory).
# ==============================================================================

include(FetchContent)

# --- OpenSSL (Cryptographic Hashing) ------------------------------------------
# OpenSSL is a system-installed library — we don't download it, we find it.
#
# find_package() searches for libraries already installed on your system.
# REQUIRED means the build will FAIL if OpenSSL isn't found.
#
# On macOS with Homebrew, OpenSSL is typically at /opt/homebrew/opt/openssl.
# We set OPENSSL_ROOT_DIR as a hint so CMake can find it, since macOS ships
# with LibreSSL by default, and Homebrew's OpenSSL isn't in the default path.
if(APPLE)
    set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl)
endif()
find_package(OpenSSL REQUIRED)

# --- secp256k1 (Elliptic Curve Cryptography) ----------------------------------
# Bitcoin Core's library for secp256k1 elliptic curve operations.
# Used for: key generation, ECDSA signing, ECDSA verification.
#
# This is the SAME library used by Bitcoin and Ethereum clients.
# We disable tests/benchmarks/exhaustive tests to keep build times short.
# https://github.com/bitcoin-core/secp256k1
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    secp256k1
    GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    TRUE
)

# --- spdlog (Logging) ---------------------------------------------------------
# Fast, header-only C++ logging library.
# We use it for structured logging across all TitanCore modules.
# https://github.com/gabime/spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
    GIT_SHALLOW    TRUE       # Only fetch the tagged commit, not full history
)

# --- nlohmann/json (JSON Serialization) ---------------------------------------
# The most developer-friendly C++ JSON library.
# We use it for serializing transactions, blocks, and RPC messages.
# https://github.com/nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# --- GoogleTest (Testing Framework) ------------------------------------------
# Industry-standard C++ testing framework.
# Provides TEST() macros, assertions (EXPECT_EQ, ASSERT_TRUE, etc.), and
# test fixtures for organized test suites.
# https://github.com/google/googletest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
    GIT_SHALLOW    TRUE
)

# --- LevelDB (Key-Value Storage) ---------------------------------------------
# A fast key-value storage library created by Jeff Dean and Sanjay Ghemawat
# at Google (the same engineers behind MapReduce and BigTable).
#
# Originally used by Bitcoin Core for block/transaction indexing.
# We disable tests, benchmarks, and installation to keep build times short.
# https://github.com/google/leveldb
set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEVELDB_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    leveldb
    GIT_REPOSITORY https://github.com/google/leveldb.git
    GIT_TAG        1.23
    GIT_SHALLOW    TRUE
)

# --- ASIO (Standalone Networking) ---------------------------------------------

# Asynchronous I/O library by Christopher Kohlhoff — the de facto standard
# for C++ networking. It's the basis of the C++ Networking TS.
#
# We use the STANDALONE version (no Boost dependency). It's header-only,
# so we just need the include path — no compilation step.
#
# ASIO uses an event-driven model:
#   io_context::run()  → event loop (blocks until work is done)
#   async_read/write   → non-blocking operations with callbacks
#   post()             → schedule work on the event loop thread
#
# https://github.com/chriskohlhoff/asio
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
)
# ASIO is header-only — we use Populate (not MakeAvailable) because ASIO
# doesn't ship a standard CMakeLists.txt we want to add to our build.
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()

# Create an INTERFACE library so consumers can just link against "asio_lib"
add_library(asio_lib INTERFACE)
target_include_directories(asio_lib INTERFACE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(asio_lib INTERFACE
    ASIO_STANDALONE        # No Boost dependency
    ASIO_NO_DEPRECATED     # Don't use deprecated APIs
)
find_package(Threads REQUIRED)
target_link_libraries(asio_lib INTERFACE Threads::Threads)

# --- Download and make all dependencies available ---
# This is where the actual downloading happens (on first cmake configure).
# Subsequent configures use the cached versions in build/_deps/.
# Note: ASIO is NOT in this list — it's handled separately above.
FetchContent_MakeAvailable(spdlog json googletest secp256k1 leveldb)
