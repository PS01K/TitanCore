#pragma once

// =============================================================================
// TitanCore — Version Information
// =============================================================================
//
// This header defines compile-time version constants for TitanCore.
//
// #pragma once:
//   This is a header guard. It tells the compiler "only include this file
//   once per translation unit." Without it, if two files both #include this
//   header, the compiler would see duplicate definitions and error out.
//   The traditional alternative is #ifndef/#define/#endif guards, but
//   #pragma once is simpler and supported by all modern compilers.
//
// constexpr:
//   These are compile-time constants. The compiler replaces them with their
//   values at compile time, so there's zero runtime cost. Think of them as
//   type-safe #defines.
// =============================================================================

#include <string_view>

namespace titancore {

constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

// std::string_view is a lightweight, non-owning reference to a string.
// Unlike std::string, it doesn't allocate memory on the heap.
// Perfect for compile-time string constants.
constexpr std::string_view VERSION_STRING = "0.1.0";
constexpr std::string_view PROJECT_NAME   = "TitanCore";
constexpr std::string_view CURRENCY_NAME  = "ODM";
constexpr std::string_view CURRENCY_SYMBOL = "ODM";

} // namespace titancore
