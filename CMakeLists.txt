# ==============================================================================
# TitanCore — Root CMake Configuration
# ==============================================================================
#
# This is the top-level build file for TitanCore. It:
#   1. Sets the minimum CMake version and project metadata
#   2. Configures the C++ standard
#   3. Fetches external dependencies
#   4. Defines the main library (titancore_lib) containing all module code
#   5. Defines the executable (titancore_node) that links against the library
#   6. Includes the test subdirectory
#
# BUILD INSTRUCTIONS:
#   mkdir -p build && cd build
#   cmake ..
#   cmake --build .
#
# ==============================================================================

cmake_minimum_required(VERSION 3.20)

project(TitanCore
    VERSION 0.1.0
    DESCRIPTION "A permissioned blockchain with native ODM cryptocurrency"
    LANGUAGES CXX
)

# --- C++ Standard Configuration -----------------------------------------------
# We use C++17 for modern features like std::optional, std::string_view,
# structured bindings, and std::filesystem.
#
# CMAKE_CXX_STANDARD_REQUIRED ensures the build FAILS if the compiler
# doesn't support C++17, rather than silently falling back to an older standard.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)    # Don't use GNU extensions, keep it portable

# --- Fetch External Dependencies ----------------------------------------------
include(cmake/Dependencies.cmake)

# --- Define the TitanCore Library ---------------------------------------------
# We build all TitanCore source code as a STATIC library. This has two benefits:
#   1. The main executable (titancore_node) links against it
#   2. The test executable ALSO links against it, so tests can access all code
#
# As we add modules (crypto, core, blockchain, etc.), we'll add their .cpp
# files to this list.
add_library(titancore_lib STATIC
    # --- Crypto module ---
    src/crypto/hash.cpp
    src/crypto/keys.cpp
    # --- Core module ---
    src/core/transaction.cpp
    src/core/block.cpp
    src/core/blockchain.cpp
    src/core/state.cpp
    src/core/mempool.cpp
)

# Tell the compiler where to find our header files.
# PUBLIC means both this library AND anything that links against it
# can use #include "titancore/common/types.hpp" etc.
target_include_directories(titancore_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link our dependencies into the library.
# PUBLIC: spdlog and nlohmann_json are used in our headers, so consumers
#         of titancore_lib also need access to them.
target_link_libraries(titancore_lib PUBLIC
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto          # SHA-256 hashing (libcrypto)
    secp256k1                # Elliptic curve key ops + ECDSA
)

# --- Define the Main Executable -----------------------------------------------
# This is the TitanCore node process — what you actually run.
add_executable(titancore_node
    src/main.cpp
)

target_link_libraries(titancore_node PRIVATE
    titancore_lib
)

# --- Tests --------------------------------------------------------------------
# enable_testing() tells CMake to generate CTest infrastructure.
# The add_subdirectory(tests) includes the test CMakeLists.txt.
enable_testing()
add_subdirectory(tests)
