A minimal c2 / remote-administration system for Linux written in
portable C.

This tool is deliberately small: two single-binary programs (operator side and
managed-host side) that share one documented wire protocol, with no
dependencies beyond the C standard library and POSIX sockets.

Table of contents:

- [Components](#components)
- [Requirements](#requirements)
- [Build](#build)
- [Quick start](#quick-start)
- [Usage](#usage)
- [Configuration](#configuration)
- [Wire protocol](#wire-protocol-v1)
- [Testing](#testing)
- [Project layout](#project-layout)
- [Security considerations](#security-considerations)
- [Exit codes](#exit-codes)

Components:

| File | Role |
|---|---|
| `controller.c` | Operator side. Listens for an agent connection, authenticates it against a shared secret, then runs an interactive prompt: each line you type is executed on the managed host and its output is printed back. |
| `agent.c` | Managed-host side. Dials out to the controller (no listening port needed), executes received commands with `popen()`, and streams the output back as length-prefixed frames. Supports optional reconnect logic. |
| `protocol.h` / `protocol.c` | Single canonical definition of the wire format: banner/key handshake, LF-terminated command lines, and `<8-digit length><payload>` response frames. Linked into both binaries so the two sides can never drift apart. |
| `net.h` | Header-only (`static inline`) socket helpers: `send_all()` / `recv_all()` handling partial writes and `EINTR`. |
| `tests/` | Unit tests (with a mocked libc `send()` via `-Wl,--wrap`) and an end-to-end test that drives the real binaries over TCP loopback, including a mock controller. |

Requirements:

- Linux (POSIX sockets, `popen`, `getopt_long`)
- A C compiler (`cc`/`gcc`/`clang`) and GNU `make`
- `bash` for the end-to-end test script

Build:

```bash
make            # builds bin/controller and bin/agent
make test       # builds everything and runs unit + end-to-end tests
make clean      # removes the bin/ directory
```

Both programs link only against libc. The shared objects (`bin/protocol.o`) are
compiled once from `protocol.c` and linked into each binary.

Testing mode:

```bash
# Terminal 1 — operator
./bin/controller --no-auth -p 4444

# Terminal 2 — managed host
./bin/agent --no-auth 127.0.0.1 4444
```

Type commands at the controller prompt; their output appears there. Type
`exit` to shut the session down cleanly.

With a shared secret (recommended for anything beyond loopback):

```bash
# Terminal 1
CONTROLLER_KEY=s3cret ./bin/controller -p 4444

# Terminal 2
AGENT_KEY=s3cret ./bin/agent 203.0.113.10 4444
```

One-shot command execution (agent connects, runs, disconnects):

```bash
./bin/controller --no-auth -c 'uname -a'
```

Usage - Controller

```
usage: controller [options]
  -p, --port PORT     TCP port to listen on (default 4444)
  -b, --bind ADDRESS  local address to bind (default: all)
  -k, --key KEY       shared secret the agent must present
      --no-auth       accept agents without a secret (loopback only)
      --allow-open    allow --no-auth on a non-loopback address
  -c, --command CMD   run one command, print the result, disconnect
  -1, --one-shot      serve a single agent, then exit (default)
      --multi         keep listening for further agents
  -t, --timeout SEC   response timeout in seconds (0 = forever)
  -v, --version       print version and exit
  -h, --help          show this help
```

Session verbs at the prompt: any line other than `exit` is sent verbatim to the
agent's shell; an empty line is ignored (keepalive); `exit` ends the session.

Usage - Agent

```
usage: agent [options] [<controller-host>] [<port>]
  -k, --key KEY      shared secret required by the controller
      --no-auth      skip the shared secret (localhost testing only)
  -r, --reconnect N  reconnect after disconnects (N attempts, 0 = infinite)
      --once         do not reconnect (default)
  -v, --version      print version and exit
  -h, --help         show this help
```

Defaults when no positional arguments are given: host `127.0.0.1`, port `4444`
(both overridable at build time — see below).

Config:

Every option has an environment-variable fallback (CLI options win):

| Variable | Effect |
|---|---|
| `CONTROLLER_PORT` / `CONTROLLER_BIND` / `CONTROLLER_KEY` | controller port, bind address, shared secret |
| `AGENT_HOST` / `AGENT_PORT` / `AGENT_KEY` | agent's controller address, port, shared secret |
| `AGENT_RECONNECT` | `1`/`true` enables unlimited reconnect retries |

Compile-time defaults can be overridden through `CFLAGS`:

```bash
make CFLAGS="-Wall -O2 -DDEFAULT_PORT=9001"
make CFLAGS='-Wall -O2 -DDEFAULT_HOST="\"203.0.113.10\""'
```

Tunable protocol constants (also `-D`-overridable): `BUF_SIZE` (4096),
`PROTO_MAX_FRAME`, `LINE_MAX_LEN` (256), `MAX_KEY_LEN` (128).

Wire protocol (V1)

Transport: TCP. All frames are ASCII. Full specification lives in the header
comment of [`protocol.h`](protocol.h).

1. **Handshake** (the controller speaks first):
   ```
   controller -> agent : "V1 \n"        (banner)
   agent      -> controller : "<key>\n" (shared secret; empty = no auth)
   controller -> agent : "OK\n" | "DENIED\n"
   ```
2. **Commands** (controller → agent), one LF-terminated line each:
   - `<shell command>\n` — executed with `popen()` on the managed host
   - `exit\n` — agent shuts down and closes the session
   - `\n` — ignored (keepalive)
3. **Responses** (agent → controller), length-prefixed frames:
   ```
   <8 zero-padded decimal byte count><payload>
   ```
   A zero-length frame is sent when a command produces no output, so the
   controller never blocks waiting for data that will not arrive.

Limits: handshake lines are at most `LINE_MAX_LEN-1` bytes; response payloads
are truncated to `PROTO_MAX_FRAME` bytes. Malformed or oversized frames cause
the connection to be dropped (the stream cannot be resynchronised).

Testing

```bash
make test
```

runs three suites:

| Target | What it covers |
|---|---|
| `bin/test_send_all` | `net.h send_all()` unit tests with libc `send()` mocked via `-Wl,--wrap=send` (partial writes, `EINTR`, errors) |
| `bin/test_protocol` | `protocol.c` line/frame codec round-trips over `socketpair()` |
| `tests/e2e_test.sh` | End-to-end over TCP loopback: mock controller drives the real `bin/agent` (handshake + command round-trip), wrong-key rejection, clean `exit` shutdown, and a real `bin/controller` ↔ `bin/agent` round-trip |

Individual targets: `make test_send_all`, `make test_protocol`,
`make mock_controller`. Set `E2E_PORT` to change the port used by the e2e test.

Project layout

```
.
├── agent.c             managed-host client
├── controller.c        operator server / interactive prompt
├── protocol.c/.h       shared wire-format implementation & spec
├── net.h               header-only send_all()/recv_all() helpers
├── Makefile            build, test, and clean targets
├── bin/                build output (gitignored)
└── tests/
    ├── e2e_test.sh         end-to-end suite over loopback TCP
    ├── mock_controller.c   scripted controller used by the e2e test
    ├── test_protocol.c     framing unit tests (socketpair)
    ├── test_send_all.c     send_all() unit tests
    └── send_mock.c/.h      --wrap=send libc mock
```

Security considerations:

- **Clear-text secret.** The V1 handshake sends the shared key unencrypted. Use
  it only on trusted networks or inside a tunnel (VPN, `ssh -L <port>:...`).
- **No-auth guardrails.** `--no-auth` refuses to bind beyond loopback unless
  `--allow-open` is explicitly passed.
- **Shell execution.** Commands run with the agent's privileges via `popen()`;
  there is no sandboxing, encryption, or output integrity protection.
- **Input limits.** Fixed-size buffers and frame caps prevent trivial overflow
  issues, but the protocol offers no replay protection or forward secrecy.

Exit codes:

Both binaries follow the same convention:

| Code | Meaning |
|---|---|
| 0 | normal end (session closed / command completed) |
| 1 | runtime or network error |
| 2 | bad usage (invalid options) |
