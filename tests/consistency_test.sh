#!/bin/sh
# tests/consistency_test.sh
# The send_all() helper is duplicated in agent.c and controller.c. This test
# guards against the two copies silently drifting apart: it extracts each
# function body and fails if they are not identical.
set -u

cd "$(dirname "$0")/.." || exit 1

extract() {
    # print lines from the send_all definition until its closing brace
    awk '/^static int send_all\(/ {p=1} p {print} p && /^\}$/ {exit}' "$1"
}

a=$(extract agent.c)
c=$(extract controller.c)

if [ -z "$a" ]; then
    echo "[FAIL] could not extract send_all() from agent.c"
    exit 1
fi
if [ -z "$c" ]; then
    echo "[FAIL] could not extract send_all() from controller.c"
    exit 1
fi

if [ "$a" = "$c" ]; then
    echo "[ ok ] send_all() implementations in agent.c and controller.c are identical"
    exit 0
else
    echo "[FAIL] send_all() implementations differ between agent.c and controller.c:"
    printf '%s\n' "$a" > /tmp/send_all_agent.$$.txt
    printf '%s\n' "$c" > /tmp/send_all_controller.$$.txt
    diff /tmp/send_all_agent.$$.txt /tmp/send_all_controller.$$.txt
    rm -f /tmp/send_all_agent.$$.txt /tmp/send_all_controller.$$.txt
    exit 1
fi