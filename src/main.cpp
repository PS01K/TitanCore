// =============================================================================
// TitanCore Node — Entry Point
// =============================================================================
//
// This is the main() function — where the TitanCore node process starts.
//
// Right now it demonstrates the full transaction lifecycle:
//   1. Users submit transactions to the mempool
//   2. Validator pulls transactions and creates a block
//   3. Block is added to the chain and state is updated
//   4. Mempool cleans up mined transactions
//   5. An overspend is rejected at the mempool level
//
// In future milestones, this will evolve into the actual node process.
// =============================================================================

#include "titancore/common/version.hpp"
#include "titancore/crypto/hash.hpp"
#include "titancore/crypto/keys.hpp"
#include "titancore/core/transaction.hpp"
#include "titancore/core/block.hpp"
#include "titancore/core/blockchain.hpp"
#include "titancore/core/state.hpp"
#include "titancore/core/mempool.hpp"
#include <spdlog/spdlog.h>

int main() {
    spdlog::set_level(spdlog::level::info);

    spdlog::info("=============================================");
    spdlog::info("  {} v{}", titancore::PROJECT_NAME, titancore::VERSION_STRING);
    spdlog::info("  Native Currency: {} ({})",
                 titancore::CURRENCY_NAME, titancore::CURRENCY_SYMBOL);
    spdlog::info("=============================================");

    using namespace titancore;
    using namespace titancore::crypto;
    using namespace titancore::core;

    // ---- Step 1: Create participants ----

    spdlog::info("");
    spdlog::info("[Lifecycle Demo] Creating participants...");

    KeyPair validator = generateKeyPair();
    Address validatorAddr = deriveAddress(validator.publicKey);
    spdlog::info("  Validator: {}", toHex(validatorAddr));

    KeyPair alice = generateKeyPair();
    Address aliceAddr = deriveAddress(alice.publicKey);
    spdlog::info("  Alice:     {}", toHex(aliceAddr));

    KeyPair bob = generateKeyPair();
    Address bobAddr = deriveAddress(bob.publicKey);
    spdlog::info("  Bob:       {}", toHex(bobAddr));

    // ---- Step 2: Initialize blockchain, state, and mempool ----

    spdlog::info("");
    Blockchain chain(validator);

    AddressMap<uint64_t> allocations;
    allocations[aliceAddr] = 10000;
    allocations[bobAddr] = 10000;
    allocations[validatorAddr] = 10000;
    StateManager state(allocations);

    Mempool mempool(state);

    spdlog::info("");
    spdlog::info("[Lifecycle Demo] Initial state:");
    spdlog::info("  Alice: {} ODM | Bob: {} ODM | Mempool: {} pending",
                 state.getBalance(aliceAddr), state.getBalance(bobAddr),
                 mempool.size());

    // ---- Step 3: Users submit transactions to mempool ----

    spdlog::info("");
    spdlog::info("[Lifecycle Demo] Submitting transactions to mempool...");

    // Alice submits two sequential transactions (pending state tracking)
    Transaction tx1 = createTransaction(alice, bobAddr, 1500, 0);
    Transaction tx2 = createTransaction(alice, bobAddr, 500, 1);
    // Bob submits one transaction
    Transaction tx3 = createTransaction(bob, aliceAddr, 800, 0);

    mempool.addTransaction(tx1);
    mempool.addTransaction(tx2);
    mempool.addTransaction(tx3);

    spdlog::info("  Mempool size: {}", mempool.size());
    spdlog::info("  Alice pending balance: {} ODM (nonce: {})",
                 mempool.getPendingBalance(aliceAddr),
                 mempool.getPendingNonce(aliceAddr));
    spdlog::info("  Bob pending balance: {} ODM (nonce: {})",
                 mempool.getPendingBalance(bobAddr),
                 mempool.getPendingNonce(bobAddr));

    // ---- Step 4: Validator creates block from mempool ----

    spdlog::info("");
    spdlog::info("[Lifecycle Demo] Validator creating block from mempool...");

    auto blockTxs = mempool.getTransactionsForBlock(100);
    Block block1 = createBlock(validator, chain.getLatestBlock(), blockTxs);

    // ---- Step 5: Block is mined → chain + state + mempool update ----

    chain.addBlock(block1);
    state.applyBlock(block1);
    mempool.removeMinedTransactions(block1);
    mempool.revalidate();

    spdlog::info("");
    spdlog::info("[Lifecycle Demo] After block 1 mined:");
    spdlog::info("  Alice: {} ODM (nonce: {})",
                 state.getBalance(aliceAddr), state.getNonce(aliceAddr));
    spdlog::info("  Bob:   {} ODM (nonce: {})",
                 state.getBalance(bobAddr), state.getNonce(bobAddr));
    spdlog::info("  Mempool: {} pending", mempool.size());
    spdlog::info("  Chain height: {}", chain.getHeight());

    // ---- Step 6: Try to overspend — rejected at mempool level ----

    spdlog::info("");
    spdlog::info("[Lifecycle Demo] Alice tries to send 99999 ODM (overspend)...");

    Transaction badTx = createTransaction(alice, bobAddr, 99999, 2);
    bool accepted = mempool.addTransaction(badTx);

    spdlog::info("  Accepted by mempool: {}", accepted ? "YES" : "NO");
    if (!accepted) {
        spdlog::info("  Reason: {}", mempool.getLastError());
    }

    // ---- Summary ----

    spdlog::info("");
    spdlog::info("Full lifecycle operational.");
    spdlog::info("  Accounts: {} | Chain height: {} | Chain valid: {}",
                 state.getAllAccounts().size(),
                 chain.getHeight(),
                 chain.validateChain() ? "YES" : "NO");

    return 0;
}
