#!/bin/bash
# tests/e2e_test.sh
# End-to-end test of the real agent/controller binaries over TCP.
#   1. mock_controller drives bin/agent and asserts command output is returned
#   2. a python client drives the real bin/controller and asserts round-trip
set -u

BIN_DIR="${1:-bin}"
PORT=4444
TMP=$(mktemp -d)
PIDS=""
cleanup() {
    [ -n "$PIDS" ] && kill $PIDS 2>/dev/null
    wait 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

status=0

########## Test 1: agent against mock controller ##########
echo "[*] test 1: bin/agent + mock controller (command round-trip)"
./bin/mock_controller "echo UNIT_TEST_MARKER_123" > "$TMP/mock.log" 2>&1 &
MOCK_PID=$!; PIDS="$PIDS $MOCK_PID"

i=0
while [ $i -lt 50 ]; do
    grep -q MOCK_READY "$TMP/mock.log" && break
    sleep 0.1; i=$((i+1))
done

"$BIN_DIR/agent" > "$TMP/agent1.log" 2>&1 &
AGENT_PID=$!; PIDS="$PIDS $AGENT_PID"

wait $MOCK_PID 2>/dev/null
kill $AGENT_PID 2>/dev/null

if grep -q "RESPONSE_BEGINUNIT_TEST_MARKER_123" "$TMP/mock.log"; then
    echo "[ ok ] agent executed the command and sent output back"
else
    echo "[FAIL] agent did not return expected output"
    echo "--- mock log ---"; cat "$TMP/mock.log"
    echo "--- agent log ---"; cat "$TMP/agent1.log"
    status=1
fi

########## Test 2: exit handling in agent->controller direction ##########
echo "[*] test 2: 'exit' command terminates the agent loop"
./bin/mock_controller "exit" > "$TMP/mock2.log" 2>&1 &
MOCK_PID=$!; PIDS="$PIDS $MOCK_PID"
i=0
while [ $i -lt 50 ]; do
    grep -q MOCK_READY "$TMP/mock2.log" && break
    sleep 0.1; i=$((i+1))
done
timeout 5 "$BIN_DIR/agent" > "$TMP/agent2.log" 2>&1
rc=$?
wait $MOCK_PID 2>/dev/null
if [ $rc -eq 0 ] && grep -q "RESPONSE_EMPTY" "$TMP/mock2.log"; then
    echo "[ ok ] agent exits cleanly on 'exit' (no output sent, rc=0)"
else
    echo "[FAIL] agent did not handle 'exit' as expected (rc=$rc)"
    echo "--- mock log ---"; cat "$TMP/mock2.log"
    echo "--- agent log ---"; cat "$TMP/agent2.log"
    status=1
fi

########## Test 3: real controller + real agent via python client ##########
echo "[*] test 3: bin/controller + bin/agent end-to-end with scripted operator"
python3 - "$BIN_DIR" "$TMP" <<'PYEOF'
import os, re, subprocess, sys, time

bin_dir, tmp = sys.argv[1], sys.argv[2]

# The controller's socket API names are injected into the source so the port
# can be overridden for tests (default remains 4444).
def build(src, out, port):
    # Test-only rebuild: override the PORT macro and rename libc socket calls to
    # __real_*, then link with --wrap so they resolve back to the real libc ones.
    code = open(src).read()
    for name in SYSCALLS:
        code = code.replace("%s(" % name, "__real_%s(" % name)
    code = re.sub(r'#define\s+PORT\s+\d+', '#define PORT %d' % port, code)
    path = os.path.join(tmp, src.rsplit("/", 1)[-1].replace(".c", "_test.c"))
    with open(path, "w") as f:
        f.write(code)
    cmd = ["gcc", "-O0", "-w", path, "-o", out]
    for n in SYSCALLS:
        cmd.append("-Wl,--wrap=" + n)
    subprocess.check_call(cmd)

SYSCALLS = ("socket", "bind", "listen", "accept", "recv", "send", "setsockopt")

build(os.path.join(bin_dir, "..", "controller.c"), os.path.join(tmp, "controller8080"), 8080)
build(os.path.join(bin_dir, "..", "agent.c"), os.path.join(tmp, "agent8080"), 8080)

ctrl_log = open(os.path.join(tmp, "ctrl3.log"), "w+")
agent_log = open(os.path.join(tmp, "agent3.log"), "w+")
ctrl = subprocess.Popen([os.path.join(tmp, "controller8080")],
                        stdin=subprocess.PIPE, stdout=ctrl_log,
                        stderr=subprocess.STDOUT, text=True)
time.sleep(0.5)
agent = subprocess.Popen([os.path.join(tmp, "agent8080")],
                         stdout=agent_log, stderr=subprocess.STDOUT, text=True)

deadline = time.time() + 5
connected = False
while time.time() < deadline:
    ctrl_log.flush()
    if "Agent connected" in open(os.path.join(tmp, "ctrl3.log")).read():
        connected = True
        break
    time.sleep(0.1)
if not connected:
    print("[FAIL] controller never reported an agent connection")
    print("--- controller ---"); print(open(os.path.join(tmp, "ctrl3.log")).read())
    print("--- agent ---"); print(open(os.path.join(tmp, "agent3.log")).read())
    ctrl.kill(); agent.kill(); sys.exit(1)

time.sleep(0.3)
ctrl.stdin.write("echo E2E_MARKER_456\n")
ctrl.stdin.flush()
time.sleep(1.0)
out = open(os.path.join(tmp, "ctrl3.log")).read()
if "E2E_MARKER_456" in out:
    print("[ ok ] controller received command output from agent")
else:
    print("[FAIL] marker not found in controller output")
    print("--- controller ---"); print(out)
    print("--- agent ---"); print(open(os.path.join(tmp, "agent3.log")).read())
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
    sys.exit(1)
sys.exit(0)
PYEOF
[ $? -ne 0 ] && status=1

echo "e2e: $([ $status -eq 0 ] && echo PASSED || echo FAILED)"
exit $status