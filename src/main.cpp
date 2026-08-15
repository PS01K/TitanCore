// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it just prints version info to verify the build works.
// In future milestones, this will:
//   1. Parse command-line arguments (node config, data directory, etc.)
//   2. Initialize the storage layer
//   3. Load or create the genesis block
//   4. Start the P2P network server
//   5. Start the RPC server
//   6. Enter the main event loop
//
// spdlog usage:
//   spdlog is a logging library. Instead of using std::cout everywhere
//   (which has no log levels, no timestamps, no formatting), spdlog gives us:
//     - Log levels: trace, debug, info, warn, error, critical
//     - Automatic timestamps
//     - Colored console output
//     - The ability to log to files later
//
//   spdlog::info("message {}",  value)  uses Python-style {} formatting
//   via the fmt library under the hood.
// =============================================================================

#include "titancore/common/version.hpp"
#include <spdlog/spdlog.h>

int main() {
    // Set the global log level. "info" means we'll see info, warn, error,
    // and critical messages, but not debug or trace.
    spdlog::set_level(spdlog::level::info);

    spdlog::info("=============================================");
    spdlog::info("  {} v{}", titancore::PROJECT_NAME, titancore::VERSION_STRING);
    spdlog::info("  Native Currency: {} ({})",
                 titancore::CURRENCY_NAME, titancore::CURRENCY_SYMBOL);
    spdlog::info("=============================================");
    spdlog::info("Node starting...");

    // TODO: In future milestones, this is where we'll initialize
    //       storage, load the blockchain, start networking, etc.

    spdlog::info("No components initialized yet (Milestone 0 — project skeleton).");
    spdlog::info("Build successful! Ready for Milestone 1: Cryptographic Keys.");

    return 0;
}
