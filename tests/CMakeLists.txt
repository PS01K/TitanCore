# ==============================================================================
# TitanCore — Test Configuration
# ==============================================================================
#
# This CMakeLists.txt defines the test executable and registers it with CTest.
#
# Key concepts:
#   - add_executable(): Creates the test binary
#   - target_link_libraries(): Links it against GoogleTest AND our library
#   - gtest_discover_tests(): Tells CTest to auto-discover all TEST() macros
#     in the binary, so each test shows up individually in test output.
# ==============================================================================

add_executable(titancore_tests
    test_main.cpp
    crypto/test_hash.cpp
    crypto/test_keys.cpp
    core/test_transaction.cpp
    core/test_block.cpp
    core/test_blockchain.cpp
)

# Link against GoogleTest and our library.
# GTest::gtest_main provides the main() function for tests, so we don't
# need to write our own. GTest::gtest provides the actual testing macros.
target_link_libraries(titancore_tests PRIVATE
    titancore_lib
    GTest::gtest
    GTest::gtest_main
)

# Auto-discover tests from the compiled binary.
# This means every TEST() and TEST_F() macro automatically becomes a
# CTest test case — no manual registration needed.
include(GoogleTest)
gtest_discover_tests(titancore_tests)
