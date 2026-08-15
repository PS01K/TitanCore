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

# --- Download and make all dependencies available ---
# This is where the actual downloading happens (on first cmake configure).
# Subsequent configures use the cached versions in build/_deps/.
FetchContent_MakeAvailable(spdlog json googletest)
