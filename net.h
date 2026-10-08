/* net.h
 * Framing helpers shared by agent.c and controller.c.
 *
 * Both programs used to carry their own private copy of send_all(); this
 * header is the single canonical implementation so the two sides of the
 * administration channel can never drift apart. It is header-only (static
 * inline) so nothing beyond the C standard library is required and each
 * binary still links against libc only.
 */
#ifndef RAT_NET_H
#define RAT_NET_H

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Send all bytes, handling partial writes and EINTR.
 * Returns 0 on success, -1 on error. */
static inline int send_all(int sock, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(sock, buf + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("send");
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

/* Receive exactly len bytes. Returns 0 on success, -1 on EOF or error. */
static inline int recv_all(int sock, char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(sock, buf + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("recv");
            return -1;
        }
        if (n == 0)
            return -1; /* peer closed mid-frame */
        total += (size_t)n;
    }
    return 0;
}

#endif /* RAT_NET_H */