// =============================================================================
// TitanCore — Test Suite Entry Point
// =============================================================================
//
// This file contains our first test, which simply verifies that:
//   1. GoogleTest is properly linked and working
//   2. Our titancore_lib headers are accessible from test code
//   3. The version constants are set correctly
//
// GoogleTest basics:
//   TEST(GroupName, TestName) { ... }
//     - GroupName: logical grouping (shows as "GroupName.TestName" in output)
//     - TestName: specific test within the group
//
//   EXPECT_EQ(actual, expected):
//     - Checks equality. If it fails, the test CONTINUES running.
//     - Use ASSERT_EQ if you want the test to STOP on failure.
//
//   EXPECT_FALSE(condition):
//     - Passes if the condition is false.
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/common/version.hpp"
#include "titancore/common/types.hpp"

// --- Version Tests -----------------------------------------------------------

TEST(VersionTest, HasCorrectMajorVersion) {
    EXPECT_EQ(titancore::VERSION_MAJOR, 0);
}

TEST(VersionTest, HasCorrectMinorVersion) {
    EXPECT_EQ(titancore::VERSION_MINOR, 1);
}

TEST(VersionTest, HasCorrectVersionString) {
    EXPECT_EQ(titancore::VERSION_STRING, "0.1.0");
}

TEST(VersionTest, HasCorrectProjectName) {
    EXPECT_EQ(titancore::PROJECT_NAME, "TitanCore");
}

TEST(VersionTest, HasCorrectCurrencyInfo) {
    EXPECT_EQ(titancore::CURRENCY_NAME, "ODM");
    EXPECT_EQ(titancore::CURRENCY_SYMBOL, "ODM");
}

// --- Type Alias Tests --------------------------------------------------------
// These tests verify that our type aliases have the correct sizes.
// This is important because cryptographic operations depend on exact byte sizes.

TEST(TypesTest, HashIs32Bytes) {
    // SHA-256 output is always 256 bits = 32 bytes
    EXPECT_EQ(sizeof(titancore::Hash), 32);
}

TEST(TypesTest, AddressIs20Bytes) {
    // Blockchain addresses are 160 bits = 20 bytes
    EXPECT_EQ(sizeof(titancore::Address), 20);
}

TEST(TypesTest, PublicKeyIs33Bytes) {
    // Compressed secp256k1 public key: 1 byte prefix + 32 bytes X coordinate
    EXPECT_EQ(sizeof(titancore::PublicKey), 33);
}

TEST(TypesTest, PrivateKeyIs32Bytes) {
    // secp256k1 private key: 256 bits = 32 bytes
    EXPECT_EQ(sizeof(titancore::PrivateKey), 32);
}

TEST(TypesTest, HashDefaultInitializesToZero) {
    // std::array default-initializes to zero when value-initialized.
    // This is useful — a zero hash is our "null hash" sentinel value.
    titancore::Hash hash{};
    for (auto byte : hash) {
        EXPECT_EQ(byte, 0);
    }
}
