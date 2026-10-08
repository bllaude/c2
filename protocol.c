/* protocol.c
 * Shared wire-format helpers declared in protocol.h. Compiled into both the
 * agent and the controller so the framing rules live in exactly one place.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

/* Strip leading/trailing whitespace in place. */
void proto_trim(char *s) {
    size_t start = 0;
    size_t len;

    while (s[start] != '\0' && isspace((unsigned char)s[start]))
        start++;
    if (start > 0)
        memmove(s, s + start, strlen(s + start) + 1);

    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

/* Read one LF-terminated line from the socket.
 * Returns 0 on success, 1 when the line was truncated to fit the buffer,
 * -1 on EOF or error. */
int proto_read_line(int sock, char *out, size_t outsz) {
    size_t i = 0;

    if (outsz == 0)
        return -1;

    while (i + 1 < outsz) {
        char ch;
        if (recv_all(sock, &ch, 1) < 0)
            return -1; /* EOF or error before end of line */
        if (ch == '\n') {
            out[i] = '\0';
            return 0;
        }
        if (ch == '\r')
            continue; /* tolerate CRLF */
        out[i++] = ch;
    }

    /* Buffer full without a newline: keep what we have, drain the rest of the
     * line so the next frame is not corrupted, and report truncation. */
    for (;;) {
        char discard;
        if (recv_all(sock, &discard, 1) < 0)
            return -1;
        if (discard == '\n')
            break;
    }
    out[i] = '\0';
    return 1;
}

/* Write a single LF-terminated line. */
int proto_write_line(int sock, const char *line) {
    if (send_all(sock, line, strlen(line)) < 0)
        return -1;
    if (send_all(sock, "\n", 1) < 0)
        return -1;
    return 0;
}

/* Write one length-prefixed frame: "%08zu" byte count followed by payload. */
int proto_send_frame(int sock, const char *data, size_t len) {
    char header[9];

    if (len > PROTO_MAX_FRAME)
        len = PROTO_MAX_FRAME; /* clamp so header and payload always agree */

    snprintf(header, sizeof(header), "%08zu", len);
    if (send_all(sock, header, 8) < 0)
        return -1;
    if (len > 0 && send_all(sock, data, len) < 0)
        return -1;
    return 0;
}

/* Read one length-prefixed frame into buf (NUL terminated).
 * Returns the payload length, -1 on EOF/error, or -2 on a malformed or
 * oversized frame - in the -2 case the stream can no longer be resynchronised
 * and the caller must drop the connection. */
long proto_recv_frame(int sock, char *buf, size_t bufsz) {
    char header[9];
    unsigned long len;
    size_t got = 0;

    if (bufsz == 0)
        return -1;

    if (recv_all(sock, header, 8) < 0)
        return -1;
    header[8] = '\0';
    for (int i = 0; i < 8; i++) {
        if (!isdigit((unsigned char)header[i]))
            return -2;
    }
    len = strtoul(header, NULL, 10);
    if (len >= bufsz)
        return -2;

    while (got < len) {
        ssize_t n = recv(sock, buf + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("recv");
            return -1;
        }
        if (n == 0)
            return -1; /* peer closed mid-frame */
        got += (size_t)n;
    }
    buf[len] = '\0';
    return (long)len;
}