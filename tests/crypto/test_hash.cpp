// =============================================================================
// TitanCore — Hash Module Tests
// =============================================================================
//
// These tests verify that our SHA-256 implementation and hex utilities
// produce correct results.
//
// TEST VECTORS:
//   We use "known answer tests" — inputs with well-known SHA-256 outputs
//   that are published in the SHA-256 specification and verified by every
//   implementation worldwide. If our output matches, we know our code is
//   correct.
//
//   You can verify these yourself:
//     echo -n "" | sha256sum
//     echo -n "hello" | sha256sum
//     echo -n "TitanCore" | sha256sum
// =============================================================================

#include <gtest/gtest.h>
#include "titancore/crypto/hash.hpp"

using namespace titancore;
using namespace titancore::crypto;

// =============================================================================
// SHA-256 Tests
// =============================================================================

TEST(HashTest, EmptyStringHasKnownHash) {
    // The SHA-256 of an empty string is a well-known constant.
    // This is the most basic test vector from the SHA-256 specification.
    Hash hash = sha256("");
    std::string hex = toHex(hash);

    EXPECT_EQ(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(HashTest, HelloHasKnownHash) {
    // Another well-known test vector.
    Hash hash = sha256("hello");
    std::string hex = toHex(hash);

    EXPECT_EQ(hex, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST(HashTest, DifferentInputsProduceDifferentHashes) {
    // The "avalanche effect" — even tiny changes produce completely
    // different hashes.
    Hash hash1 = sha256("hello");
    Hash hash2 = sha256("Hello");  // Capital H

    EXPECT_NE(hash1, hash2);
}

TEST(HashTest, SameInputProducesSameHash) {
    // Hashing is deterministic — same input ALWAYS gives same output.
    Hash hash1 = sha256("TitanCore");
    Hash hash2 = sha256("TitanCore");

    EXPECT_EQ(hash1, hash2);
}

TEST(HashTest, HashIs32Bytes) {
    Hash hash = sha256("anything");
    EXPECT_EQ(hash.size(), 32);
}

TEST(HashTest, BytesOverloadMatchesStringOverload) {
    // Both overloads should produce the same result for the same data.
    std::string input = "TitanCore ODM";
    Bytes inputBytes(input.begin(), input.end());

    Hash hashFromString = sha256(input);
    Hash hashFromBytes = sha256(inputBytes);

    EXPECT_EQ(hashFromString, hashFromBytes);
}

TEST(HashTest, DoubleSha256ProducesDifferentFromSingle) {
    Bytes data = {'t', 'e', 's', 't'};
    Hash single = sha256(data);
    Hash doubled = doubleSha256(data);

    // Double hash should be different from single hash
    EXPECT_NE(single, doubled);

    // But double hash should equal manually doing sha256(sha256(data))
    Bytes singleBytes(single.begin(), single.end());
    Hash manualDouble = sha256(singleBytes);
    EXPECT_EQ(doubled, manualDouble);
}

// =============================================================================
// Hex Encoding/Decoding Tests
// =============================================================================

TEST(HexTest, EncodesEmptyBytesCorrectly) {
    Bytes empty;
    EXPECT_EQ(toHex(empty), "");
}

TEST(HexTest, EncodesSingleByteCorrectly) {
    Bytes data = {0xAB};
    EXPECT_EQ(toHex(data), "ab");
}

TEST(HexTest, EncodesMultipleBytesCorrectly) {
    Bytes data = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    EXPECT_EQ(toHex(data), "0123456789abcdef");
}

TEST(HexTest, EncodesLeadingZeroCorrectly) {
    // Leading zeros must be preserved — 0x00 → "00", not "0" or ""
    Bytes data = {0x00, 0xFF};
    EXPECT_EQ(toHex(data), "00ff");
}

TEST(HexTest, DecodesValidHex) {
    Bytes result = fromHex("a1b2c3");
    Bytes expected = {0xA1, 0xB2, 0xC3};
    EXPECT_EQ(result, expected);
}

TEST(HexTest, DecodesUppercaseHex) {
    // fromHex should handle both lowercase and uppercase
    Bytes lower = fromHex("abcd");
    Bytes upper = fromHex("ABCD");
    EXPECT_EQ(lower, upper);
}

TEST(HexTest, RejectsOddLengthHex) {
    // Hex strings must have even length (2 chars per byte)
    Bytes result = fromHex("abc");
    EXPECT_TRUE(result.empty());
}

TEST(HexTest, RejectsInvalidCharacters) {
    Bytes result = fromHex("gg");
    EXPECT_TRUE(result.empty());
}

TEST(HexTest, RoundTrip) {
    // Encoding then decoding should give back the original data
    Bytes original = {0xDE, 0xAD, 0xBE, 0xEF};
    std::string hex = toHex(original);
    Bytes decoded = fromHex(hex);
    EXPECT_EQ(original, decoded);
}

TEST(HexTest, FixedSizeArrayEncoding) {
    // Test the template version with a Hash (std::array<uint8_t, 32>)
    Hash hash{};
    hash[0] = 0xAB;
    hash[31] = 0xCD;

    std::string hex = toHex(hash);
    EXPECT_EQ(hex.size(), 64);         // 32 bytes → 64 hex chars
    EXPECT_EQ(hex.substr(0, 2), "ab"); // First byte
    EXPECT_EQ(hex.substr(62, 2), "cd"); // Last byte
}
