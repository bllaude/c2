/* tests/test_protocol.c
 * Unit tests for the wire-format helpers in protocol.c using a socketpair(),
 * so frames are exercised over a real kernel buffer without any network. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "../protocol.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            printf("  [FAIL] %s\n", (msg));                                    \
            failures++;                                                        \
        } else {                                                               \
            printf("  [ ok ] %s\n", (msg));                                    \
        }                                                                      \
    } while (0)

static void make_pair(int sp[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
        perror("socketpair");
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    printf("== suite: protocol.c framing ==\n");

    /* proto_trim */
    {
        char s[] = "   hello world  \r\n";
        proto_trim(s);
        CHECK(strcmp(s, "hello world") == 0, "proto_trim strips whitespace/CRLF");
    }

    /* line round trip, including CRLF tolerance and empty lines */
    {
        int sp[2]; make_pair(sp);
        char out[64];

        CHECK(proto_write_line(sp[0], "uptime -p") == 0, "write_line succeeds");
        CHECK(proto_read_line(sp[1], out, sizeof(out)) == 0, "read_line succeeds");
        CHECK(strcmp(out, "uptime -p") == 0, "line content preserved");

        /* CRLF from an external client should not leak into the value */
        const char *crlf = "whoami\r\n";
        send_all(sp[0], crlf, strlen(crlf));
        CHECK(proto_read_line(sp[1], out, sizeof(out)) == 0, "CRLF line read");
        CHECK(strcmp(out, "whoami") == 0, "CR stripped");

        /* empty line (keepalive) */
        send_all(sp[0], "\n", 1);
        CHECK(proto_read_line(sp[1], out, sizeof(out)) == 0, "empty line read");
        CHECK(out[0] == '\0', "empty line yields empty string");

        close(sp[0]); close(sp[1]);
    }

    /* oversized line is truncated and reported, stream stays resynchronised */
    {
        int sp[2]; make_pair(sp);
        char big[300];
        char out[16];
        memset(big, 'A', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        strcat(big, "\n");

        send_all(sp[0], big, strlen(big));
        int rc = proto_read_line(sp[1], out, sizeof(out));
        CHECK(rc == 1, "long line reports truncation");
        CHECK(strlen(out) == 15, "truncated line fits the buffer");

        /* next line must still arrive intact */
        proto_write_line(sp[0], "ok");
        memset(out, 0, sizeof(out));
        CHECK(proto_read_line(sp[1], out, sizeof(out)) == 0, "resynchronised after truncation");
        CHECK(strcmp(out, "ok") == 0, "second line correct");

        close(sp[0]); close(sp[1]);
    }

    /* frame round trip incl. empty payload and binary-ish content */
    {
        int sp[2]; make_pair(sp);
        char buf[BUF_SIZE];
        long n;

        CHECK(proto_send_frame(sp[0], "hello\nworld\n", 12) == 0, "send_frame");
        n = proto_recv_frame(sp[1], buf, sizeof(buf));
        CHECK(n == 12 && memcmp(buf, "hello\nworld\n", 12) == 0, "recv_frame preserves payload");

        CHECK(proto_send_frame(sp[0], "", 0) == 0, "send empty frame");
        n = proto_recv_frame(sp[1], buf, sizeof(buf));
        CHECK(n == 0, "recv empty frame returns 0 bytes");

        close(sp[0]); close(sp[1]);
    }

    /* malformed header is rejected (-2) */
    {
        int sp[2]; make_pair(sp);
        char buf[64];

        send_all(sp[0], "12345abc", 8);
        CHECK(proto_recv_frame(sp[1], buf, sizeof(buf)) == -2, "non-digit header -> -2");

        close(sp[0]); close(sp[1]);
    }

    /* oversized frame is rejected (-2) rather than overflowing the buffer */
    {
        int sp[2]; make_pair(sp);
        char small[16];

        send_all(sp[0], "00009999", 8);
        CHECK(proto_recv_frame(sp[1], small, sizeof(small)) == -2, "oversized frame -> -2");

        close(sp[0]); close(sp[1]);
    }

    /* peer closing mid-frame is an error, not a hang */
    {
        int sp[2]; make_pair(sp);
        char buf[64];

        send_all(sp[0], "00000010", 8); /* promises 10 bytes... */
        close(sp[0]);                    /* ...then dies */
        CHECK(proto_recv_frame(sp[1], buf, sizeof(buf)) == -1, "EOF mid-frame -> -1");

        close(sp[1]);
    }

    printf("\n%s: %d checks, %d failures\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}