#!/bin/bash
# tests/e2e_test.sh
# End-to-end tests of the real agent/controller binaries over TCP loopback.
#   1. mock controller performs the V1 handshake and drives bin/agent
#   2. wrong-key rejection
#   3. 'exit' shuts the agent down cleanly
#   4. real bin/controller + bin/agent round-trip, including clean shutdown
set -u

BIN_DIR="${1:-bin}"
PORT=${E2E_PORT:-4444}
TMP=$(mktemp -d)
PIDS=""
cleanup() {
    [ -n "$PIDS" ] && kill $PIDS 2>/dev/null
    wait 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

status=0

########## Test 1: agent + mock controller (handshake + command round-trip) ##########
echo "[*] test 1: bin/agent + mock controller (command round-trip)"
"$BIN_DIR/mock_controller" -k "" -p "$PORT" "echo UNIT_TEST_MARKER_123" > "$TMP/mock.log" 2>&1 &
MOCK_PID=$!; PIDS="$PIDS $MOCK_PID"

i=0
while [ $i -lt 50 ]; do
    grep -q MOCK_READY "$TMP/mock.log" && break
    sleep 0.1; i=$((i+1))
done

timeout 10 "$BIN_DIR/agent" --no-auth 127.0.0.1 "$PORT" > "$TMP/agent1.log" 2>&1 &
AGENT_PID=$!; PIDS="$PIDS $AGENT_PID"

wait $MOCK_PID 2>/dev/null
kill $AGENT_PID 2>/dev/null

if grep -q "RESPONSE_BEGINUNIT_TEST_MARKER_123" "$TMP/mock.log"; then
    echo "[ ok ] agent authenticated and returned command output"
else
    echo "[FAIL] agent did not return expected output"
    echo "--- mock log ---"; cat "$TMP/mock.log"
    echo "--- agent log ---"; cat "$TMP/agent1.log"
    status=1
fi

########## Test 2: wrong key is rejected by the controller side ##########
echo "[*] test 2: wrong key -> handshake refused"
"$BIN_DIR/mock_controller" -k "right-key" -p "$PORT" "echo should-not-run" > "$TMP/mock2.log" 2>&1 &
MOCK_PID=$!; PIDS="$PIDS $MOCK_PID"
i=0
while [ $i -lt 50 ]; do
    grep -q MOCK_READY "$TMP/mock2.log" && break
    sleep 0.1; i=$((i+1))
done
timeout 10 "$BIN_DIR/agent" -k "wrong-key" 127.0.0.1 "$PORT" > "$TMP/agent2.log" 2>&1
rc=$?
wait $MOCK_PID 2>/dev/null
if [ $rc -ne 0 ] && grep -q "MOCK_DENIED" "$TMP/mock2.log"; then
    echo "[ ok ] agent with wrong key is denied (rc=$rc)"
else
    echo "[FAIL] wrong key was not handled correctly (rc=$rc)"
    echo "--- mock log ---"; cat "$TMP/mock2.log"
    echo "--- agent log ---"; cat "$TMP/agent2.log"
    status=1
fi

########## Test 3: exit handling in agent->controller direction ##########
echo "[*] test 3: 'exit' command terminates the agent loop"
"$BIN_DIR/mock_controller" -k "" -p "$PORT" "exit" > "$TMP/mock3.log" 2>&1 &
MOCK_PID=$!; PIDS="$PIDS $MOCK_PID"
i=0
while [ $i -lt 50 ]; do
    grep -q MOCK_READY "$TMP/mock3.log" && break
    sleep 0.1; i=$((i+1))
done
timeout 10 "$BIN_DIR/agent" --no-auth 127.0.0.1 "$PORT" > "$TMP/agent3.log" 2>&1
rc=$?
wait $MOCK_PID 2>/dev/null
if [ $rc -eq 0 ] && grep -q "RESPONSE_EMPTY" "$TMP/mock3.log"; then
    echo "[ ok ] agent exits cleanly on 'exit' (no output sent, rc=0)"
else
    echo "[FAIL] agent did not handle 'exit' as expected (rc=$rc)"
    echo "--- mock log ---"; cat "$TMP/mock3.log"
    echo "--- agent log ---"; cat "$TMP/agent3.log"
    status=1
fi

########## Test 4: real controller + real agent round-trip ##########
echo "[*] test 4: bin/controller + bin/agent end-to-end with scripted operator"
python3 - "$BIN_DIR" "$TMP" "$PORT" <<'PYEOF'
import os, subprocess, sys, time

bin_dir, tmp, port = sys.argv[1], sys.argv[2], sys.argv[3]
port = int(port)

ctrl_log = open(os.path.join(tmp, "ctrl4.log"), "w+")
agent_log = open(os.path.join(tmp, "agent4.log"), "w+")

ctrl = subprocess.Popen([os.path.join(bin_dir, "controller"),
                         "--no-auth", "-b", "127.0.0.1", "-p", str(port)],
                        stdin=subprocess.PIPE, stdout=ctrl_log,
                        stderr=subprocess.STDOUT, text=True)
time.sleep(0.5)
agent = subprocess.Popen([os.path.join(bin_dir, "agent"), "--no-auth",
                          "127.0.0.1", str(port)],
                         stdout=agent_log, stderr=subprocess.STDOUT, text=True)

def log(path):
    try:
        return open(path).read()
    except OSError:
        return ""

deadline = time.time() + 5
connected = False
while time.time() < deadline:
    ctrl_log.flush()
    if "Agent connected" in log(os.path.join(tmp, "ctrl4.log")):
        connected = True
        break
    time.sleep(0.1)
if not connected:
    print("[FAIL] controller never reported an agent connection")
    print("--- controller ---"); print(log(os.path.join(tmp, "ctrl4.log")))
    print("--- agent ---"); print(log(os.path.join(tmp, "agent4.log")))
    ctrl.kill(); agent.kill(); sys.exit(1)

time.sleep(0.3)
ctrl.stdin.write("echo E2E_MARKER_456\n")
ctrl.stdin.flush()
deadline = time.time() + 5
found = False
while time.time() < deadline:
    ctrl_log.flush()
    if "E2E_MARKER_456" in log(os.path.join(tmp, "ctrl4.log")):
        found = True
        break
    time.sleep(0.1)
if not found:
    print("[FAIL] marker not found in controller output")
    print("--- controller ---"); print(log(os.path.join(tmp, "ctrl4.log")))
    print("--- agent ---"); print(log(os.path.join(tmp, "agent4.log")))
    ctrl.kill(); agent.kill(); sys.exit(1)
print("[ ok ] controller received framed command output from agent")

# multi-line output must arrive complete (frame reassembly check)
ctrl.stdin.write("printf 'L1\\nL2\\nL3\\n'\n")
ctrl.stdin.flush()
time.sleep(0.7)
ctrl_log.flush()
out = log(os.path.join(tmp, "ctrl4.log"))
if "L1" in out and "L2" in out and "L3" in out:
    print("[ ok ] multi-line output reassembled from one frame")
else:
    print("[FAIL] multi-line output lost")
    print(out)
    ctrl.kill(); agent.kill(); sys.exit(1)

ctrl.stdin.write("exit\n")
ctrl.stdin.flush()
try:
    ctrl.wait(timeout=5); rc_c = ctrl.returncode
except subprocess.TimeoutExpired:
    ctrl.kill(); rc_c = -1
try:
    agent.wait(timeout=5); rc_a = agent.returncode
except subprocess.TimeoutExpired:
    agent.kill(); rc_a = -1
if rc_c == 0 and rc_a == 0:
    print("[ ok ] both processes shut down cleanly after 'exit'")
else:
    print("[FAIL] unclean shutdown (controller rc=%s, agent rc=%s)" % (rc_c, rc_a))
    print("--- controller ---"); print(log(os.path.join(tmp, "ctrl4.log")))
    print("--- agent ---"); print(log(os.path.join(tmp, "agent4.log")))
    sys.exit(1)
sys.exit(0)
PYEOF
[ $? -ne 0 ] && status=1

echo "e2e: $([ $status -eq 0 ] && echo PASSED || echo FAILED)"
exit $status
