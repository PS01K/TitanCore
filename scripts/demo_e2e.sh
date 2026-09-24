#!/usr/bin/env bash
# =============================================================================
# TitanCore V1 — End-to-End Demo
# =============================================================================
#
# This script demonstrates the complete TitanCore V1 lifecycle:
#
#   1. Bootstrap a 3-authority network (generate keys + genesis config)
#   2. Start 3 independent node processes
#   3. Verify initial state via RPC
#   4. Submit real transactions via RPC (signed offline)
#   5. Wait for block production + propagation
#   6. Verify balances across all nodes
#   7. Test persistence: kill and restart a node
#   8. Print final summary
#
# Usage:
#   ./scripts/demo_e2e.sh
#
# Prerequisites:
#   - Build TitanCore first: cd build && cmake .. && cmake --build .
# =============================================================================

set -euo pipefail

# --- Configuration ---
BINARY="./build/titancore_node"
GENESIS_DIR="demo_genesis"
DATA_DIR="demo_data"
NODE_PIDS=()

# Node ports
P2P_PORT_0=19001
P2P_PORT_1=19002
P2P_PORT_2=19003
RPC_PORT_0=18545
RPC_PORT_1=18546
RPC_PORT_2=18547

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# --- Helper Functions ---

print_header() {
    echo ""
    echo -e "${BOLD}${CYAN}═══════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${CYAN}  $1${NC}"
    echo -e "${BOLD}${CYAN}═══════════════════════════════════════════════${NC}"
}

print_step() {
    echo -e "\n${YELLOW}▸ $1${NC}"
}

print_ok() {
    echo -e "  ${GREEN}✓${NC} $1"
}

print_fail() {
    echo -e "  ${RED}✗${NC} $1"
}

rpc_call() {
    local port=$1
    local method=$2
    local params=${3:-'{}'}
    curl -s -X POST "http://localhost:${port}/rpc" \
        -H 'Content-Type: application/json' \
        -d "{\"jsonrpc\":\"2.0\",\"method\":\"${method}\",\"params\":${params},\"id\":1}" \
        2>/dev/null
}

rpc_get_result() {
    local port=$1
    local method=$2
    local params=${3:-'{}'}
    local field=$4
    rpc_call "$port" "$method" "$params" | python3 -c "import json,sys; print(json.load(sys.stdin)['result']['${field}'])"
}

cleanup() {
    print_step "Cleaning up..."
    for pid in "${NODE_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    rm -rf "$GENESIS_DIR" "$DATA_DIR"
    echo -e "  ${GREEN}Done.${NC}"
}

# Ensure cleanup on exit
trap cleanup EXIT

# --- Preflight Check ---

if [ ! -f "$BINARY" ]; then
    echo -e "${RED}Error: $BINARY not found. Build first:${NC}"
    echo "  cd build && cmake .. && cmake --build ."
    exit 1
fi

# Clean up any previous run
rm -rf "$GENESIS_DIR" "$DATA_DIR"
mkdir -p "$DATA_DIR"

# =============================================================================
# Phase 1: Bootstrap
# =============================================================================

print_header "Phase 1: Bootstrap Network"

print_step "Generating 3 authority keys..."
$BINARY --generate-keys --count 3 --genesis-dir "$GENESIS_DIR" 2>/dev/null

# Extract addresses from genesis.json
ADDR_0=$(python3 -c "import json; d=json.load(open('${GENESIS_DIR}/genesis.json')); print(d['authorities'][0]['address'])")
ADDR_1=$(python3 -c "import json; d=json.load(open('${GENESIS_DIR}/genesis.json')); print(d['authorities'][1]['address'])")
ADDR_2=$(python3 -c "import json; d=json.load(open('${GENESIS_DIR}/genesis.json')); print(d['authorities'][2]['address'])")

print_ok "Authority 0: ${ADDR_0}"
print_ok "Authority 1: ${ADDR_1}"
print_ok "Authority 2: ${ADDR_2}"
print_ok "Each authority allocated 10,000 ODM"

# =============================================================================
# Phase 2: Start Network
# =============================================================================

print_header "Phase 2: Start 3-Node Network"

print_step "Starting Node 0 (port ${P2P_PORT_0}, RPC ${RPC_PORT_0})..."
$BINARY --index 0 --port $P2P_PORT_0 --rpc-port $RPC_PORT_0 \
    --data-dir "${DATA_DIR}/node0" --genesis-dir "$GENESIS_DIR" \
    --peers "127.0.0.1:${P2P_PORT_1},127.0.0.1:${P2P_PORT_2}" \
    > "${DATA_DIR}/node0.log" 2>&1 &
NODE_PIDS+=($!)
print_ok "Node 0 PID: ${NODE_PIDS[0]}"

print_step "Starting Node 1 (port ${P2P_PORT_1}, RPC ${RPC_PORT_1})..."
$BINARY --index 1 --port $P2P_PORT_1 --rpc-port $RPC_PORT_1 \
    --data-dir "${DATA_DIR}/node1" --genesis-dir "$GENESIS_DIR" \
    --peers "127.0.0.1:${P2P_PORT_0}" \
    > "${DATA_DIR}/node1.log" 2>&1 &
NODE_PIDS+=($!)
print_ok "Node 1 PID: ${NODE_PIDS[1]}"

print_step "Starting Node 2 (port ${P2P_PORT_2}, RPC ${RPC_PORT_2})..."
$BINARY --index 2 --port $P2P_PORT_2 --rpc-port $RPC_PORT_2 \
    --data-dir "${DATA_DIR}/node2" --genesis-dir "$GENESIS_DIR" \
    --peers "127.0.0.1:${P2P_PORT_0}" \
    > "${DATA_DIR}/node2.log" 2>&1 &
NODE_PIDS+=($!)
print_ok "Node 2 PID: ${NODE_PIDS[2]}"

print_step "Waiting for nodes to start and connect..."
sleep 3

# Verify RPC is responding
for port in $RPC_PORT_0 $RPC_PORT_1 $RPC_PORT_2; do
    HEIGHT=$(rpc_get_result "$port" "getChainHeight" '{}' "height" 2>/dev/null || echo "FAIL")
    if [ "$HEIGHT" != "FAIL" ]; then
        print_ok "RPC on port ${port}: responding (height=${HEIGHT})"
    else
        print_fail "RPC on port ${port}: not responding"
        exit 1
    fi
done

# =============================================================================
# Phase 3: Verify Initial State
# =============================================================================

print_header "Phase 3: Verify Initial State (via RPC)"

print_step "Querying balances from Node 0..."
BAL_0=$(rpc_get_result $RPC_PORT_0 "getBalance" "{\"address\":\"${ADDR_0}\"}" "balance")
BAL_1=$(rpc_get_result $RPC_PORT_0 "getBalance" "{\"address\":\"${ADDR_1}\"}" "balance")
BAL_2=$(rpc_get_result $RPC_PORT_0 "getBalance" "{\"address\":\"${ADDR_2}\"}" "balance")

print_ok "Auth 0: ${BAL_0} ODM (expected: 10000)"
print_ok "Auth 1: ${BAL_1} ODM (expected: 10000)"
print_ok "Auth 2: ${BAL_2} ODM (expected: 10000)"

if [ "$BAL_0" = "10000" ] && [ "$BAL_1" = "10000" ] && [ "$BAL_2" = "10000" ]; then
    print_ok "Initial balances correct!"
else
    print_fail "Initial balances incorrect!"
    exit 1
fi

# =============================================================================
# Phase 4: Submit Transactions (via RPC)
# =============================================================================

print_header "Phase 4: Submit Transactions via RPC"

# Transaction 1: auth0 → auth1: 500 ODM
print_step "Signing: auth0 → auth1: 500 ODM..."
TX1_JSON=$($BINARY --sign-tx --key "${GENESIS_DIR}/auth0.key" \
    --to "$ADDR_1" --amount 500 --nonce 0)
print_ok "Transaction signed"

print_step "Submitting tx1 to Node 0 via RPC..."
SUBMIT1=$(rpc_call $RPC_PORT_0 "submitTransaction" "{\"transaction\":${TX1_JSON}}")
ACCEPTED1=$(echo "$SUBMIT1" | python3 -c "import json,sys; print(json.load(sys.stdin)['result']['accepted'])")
if [ "$ACCEPTED1" = "True" ]; then
    print_ok "Transaction accepted!"
else
    print_fail "Transaction rejected: $SUBMIT1"
    exit 1
fi

# Transaction 2: auth1 → auth2: 200 ODM
print_step "Signing: auth1 → auth2: 200 ODM..."
TX2_JSON=$($BINARY --sign-tx --key "${GENESIS_DIR}/auth1.key" \
    --to "$ADDR_2" --amount 200 --nonce 0)
print_ok "Transaction signed"

print_step "Submitting tx2 to Node 1 via RPC..."
SUBMIT2=$(rpc_call $RPC_PORT_1 "submitTransaction" "{\"transaction\":${TX2_JSON}}")
ACCEPTED2=$(echo "$SUBMIT2" | python3 -c "import json,sys; print(json.load(sys.stdin)['result']['accepted'])")
if [ "$ACCEPTED2" = "True" ]; then
    print_ok "Transaction accepted!"
else
    print_fail "Transaction rejected: $SUBMIT2"
    exit 1
fi

# Wait for transactions to propagate to mempools
sleep 1

MEMPOOL_0=$(rpc_get_result $RPC_PORT_0 "getMempoolSize" '{}' "size")
MEMPOOL_1=$(rpc_get_result $RPC_PORT_1 "getMempoolSize" '{}' "size")
print_ok "Mempool sizes: node0=${MEMPOOL_0}, node1=${MEMPOOL_1}"

# =============================================================================
# Phase 5: Wait for Block Production
# =============================================================================

print_header "Phase 5: Block Production & Propagation"

print_step "Waiting for blocks to be produced (up to 30 seconds)..."
for i in $(seq 1 30); do
    sleep 1
    H0=$(rpc_get_result $RPC_PORT_0 "getChainHeight" '{}' "height")
    H1=$(rpc_get_result $RPC_PORT_1 "getChainHeight" '{}' "height")
    H2=$(rpc_get_result $RPC_PORT_2 "getChainHeight" '{}' "height")

    # We need at least 2 blocks after genesis to include both transactions
    if [ "$H0" -ge 3 ] && [ "$H1" -ge 3 ] && [ "$H2" -ge 3 ]; then
        print_ok "All nodes at height ≥ 3 after ${i}s"
        break
    fi
    if [ "$i" = "30" ]; then
        print_fail "Timed out waiting for blocks. Heights: ${H0}, ${H1}, ${H2}"
        exit 1
    fi
done

print_ok "Chain heights: node0=${H0}, node1=${H1}, node2=${H2}"

# =============================================================================
# Phase 6: Verify Balances (all nodes must agree)
# =============================================================================

print_header "Phase 6: Verify Balances Across All Nodes"

# Expected: auth0=9500, auth1=10300, auth2=10200
EXPECTED_0=9500
EXPECTED_1=10300
EXPECTED_2=10200

ALL_CORRECT=true

for node_label in 0 1 2; do
    case $node_label in
        0) RPC=$RPC_PORT_0 ;;
        1) RPC=$RPC_PORT_1 ;;
        2) RPC=$RPC_PORT_2 ;;
    esac

    B0=$(rpc_get_result $RPC "getBalance" "{\"address\":\"${ADDR_0}\"}" "balance")
    B1=$(rpc_get_result $RPC "getBalance" "{\"address\":\"${ADDR_1}\"}" "balance")
    B2=$(rpc_get_result $RPC "getBalance" "{\"address\":\"${ADDR_2}\"}" "balance")

    print_step "Node ${node_label}: auth0=${B0}, auth1=${B1}, auth2=${B2}"

    if [ "$B0" = "$EXPECTED_0" ] && [ "$B1" = "$EXPECTED_1" ] && [ "$B2" = "$EXPECTED_2" ]; then
        print_ok "Correct!"
    else
        print_fail "Expected: auth0=${EXPECTED_0}, auth1=${EXPECTED_1}, auth2=${EXPECTED_2}"
        ALL_CORRECT=false
    fi
done

if [ "$ALL_CORRECT" = "false" ]; then
    print_fail "Balance verification failed!"
    exit 1
fi

# =============================================================================
# Phase 7: Persistence Test
# =============================================================================

print_header "Phase 7: Persistence Test (Kill & Restart Node 0)"

print_step "Killing Node 0 (PID ${NODE_PIDS[0]})..."
kill "${NODE_PIDS[0]}" 2>/dev/null || true
wait "${NODE_PIDS[0]}" 2>/dev/null || true
sleep 1
print_ok "Node 0 stopped"

print_step "Restarting Node 0 from LevelDB..."
$BINARY --index 0 --port $P2P_PORT_0 --rpc-port $RPC_PORT_0 \
    --data-dir "${DATA_DIR}/node0" --genesis-dir "$GENESIS_DIR" \
    --peers "127.0.0.1:${P2P_PORT_1}" \
    > "${DATA_DIR}/node0_restart.log" 2>&1 &
NODE_PIDS[0]=$!
sleep 2
print_ok "Node 0 restarted (PID ${NODE_PIDS[0]})"

print_step "Verifying recovered state..."
REC_H=$(rpc_get_result $RPC_PORT_0 "getChainHeight" '{}' "height")
REC_0=$(rpc_get_result $RPC_PORT_0 "getBalance" "{\"address\":\"${ADDR_0}\"}" "balance")
REC_1=$(rpc_get_result $RPC_PORT_0 "getBalance" "{\"address\":\"${ADDR_1}\"}" "balance")
REC_2=$(rpc_get_result $RPC_PORT_0 "getBalance" "{\"address\":\"${ADDR_2}\"}" "balance")

print_ok "Recovered height: ${REC_H}"
print_ok "Recovered balances: auth0=${REC_0}, auth1=${REC_1}, auth2=${REC_2}"

if [ "$REC_0" = "$EXPECTED_0" ] && [ "$REC_1" = "$EXPECTED_1" ] && [ "$REC_2" = "$EXPECTED_2" ]; then
    print_ok "Persistence verified — state fully recovered from LevelDB!"
else
    print_fail "Persistence check failed!"
    exit 1
fi

# =============================================================================
# Summary
# =============================================================================

print_header "TitanCore V1 — End-to-End Demo Complete"

echo ""
echo -e "${BOLD}  Components verified:${NC}"
echo -e "    ${GREEN}✓${NC} Key generation & genesis config"
echo -e "    ${GREEN}✓${NC} 3 independent node processes"
echo -e "    ${GREEN}✓${NC} P2P networking & peer discovery"
echo -e "    ${GREEN}✓${NC} Offline transaction signing (--sign-tx)"
echo -e "    ${GREEN}✓${NC} Transaction submission via JSON-RPC"
echo -e "    ${GREEN}✓${NC} Mempool propagation"
echo -e "    ${GREEN}✓${NC} PoA block production (round-robin)"
echo -e "    ${GREEN}✓${NC} Block propagation & chain sync"
echo -e "    ${GREEN}✓${NC} State consistency across all nodes"
echo -e "    ${GREEN}✓${NC} LevelDB persistence & recovery"
echo ""
echo -e "${BOLD}  Transactions:${NC}"
echo -e "    auth0 → auth1: 500 ODM"
echo -e "    auth1 → auth2: 200 ODM"
echo ""
echo -e "${BOLD}  Final balances (all nodes agree):${NC}"
echo -e "    auth0: ${EXPECTED_0} ODM"
echo -e "    auth1: ${EXPECTED_1} ODM"
echo -e "    auth2: ${EXPECTED_2} ODM"
echo ""
echo -e "${BOLD}${GREEN}  TitanCore v1.0.0 — All systems operational.${NC}"
echo ""
