/* tests/test_send_all.c
 * Unit tests for the shared framing helpers in net.h / protocol.c.
 *
 * send_all() lives in net.h as a static inline so both binaries get their own
 * copy; to test it without touching a real socket we compile net.h into this
 * translation unit with libc's send()/recv() replaced by mocks via
 * -Wl,--wrap (see tests/send_mock.c and the Makefile `test` target). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "send_mock.h"

/* Pull in the implementation under test. Wrapping makes calls to send() here
 * resolve to __wrap_send() from tests/send_mock.c. */
#include "../net.h"

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

static void run_send_suite(void)
{
    printf("== suite: net.h::send_all ==\n");

    /* 1. zero-length payload must succeed without touching the socket */
    mock_reset();
    CHECK(send_all(42, "", 0) == 0, "[len=0] returns 0");
    CHECK(mock_calls() == 0, "[len=0] no send() call made");

    /* 2. single full write */
    mock_reset();
    mock_queue(5, 0);
    CHECK(send_all(42, "hello", 5) == 0, "[full write] returns 0");
    CHECK(mock_calls() == 1, "[full write] exactly one send() call");

    /* 3. partial writes must be resumed until everything is sent */
    mock_reset();
    mock_queue(3, 0);
    mock_queue(4, 0);
    mock_queue(2, 0);
    CHECK(send_all(42, "abcdefghi", 9) == 0, "[partial] returns 0");
    CHECK(mock_calls() == 3, "[partial] looped until all 9 bytes sent");

    /* 4. EINTR must be retried transparently */
    mock_reset();
    mock_queue(-1, EINTR);
    mock_queue(6, 0);
    CHECK(send_all(42, "secret", 6) == 0, "[EINTR] returns 0 after retry");
    CHECK(mock_calls() == 2, "[EINTR] one interrupted + one successful call");

    /* 5. repeated EINTR followed by success */
    mock_reset();
    mock_queue(-1, EINTR);
    mock_queue(-1, EINTR);
    mock_queue(4, 0);
    CHECK(send_all(42, "data", 4) == 0, "[EINTR x2] returns 0 after retries");
    CHECK(mock_calls() == 3, "[EINTR x2] retried twice then succeeded");

    /* 6. hard error must return -1 and NOT retry */
    mock_reset();
    mock_queue(-1, EPIPE);
    CHECK(send_all(42, "abc", 3) == -1, "[EPIPE] returns -1");
    CHECK(mock_calls() == 1, "[EPIPE] no retry on non-EINTR errors");

    /* 7. error after a partial write still returns -1 */
    mock_reset();
    mock_queue(2, 0);
    mock_queue(-1, ECONNRESET);
    CHECK(send_all(42, "abcdef", 6) == -1, "[partial then reset] returns -1");
    CHECK(mock_calls() == 2, "[partial then reset] stopped at first hard error");

    /* 8. offset correctness: remaining length shrinks each call */
    mock_reset();
    mock_queue(1, 0);
    mock_queue(1, 0);
    mock_queue(1, 0);
    CHECK(send_all(42, "xyz", 3) == 0, "[byte-at-a-time] returns 0");
    CHECK(mock_calls() == 3 &&
          mock_len(0) == 3 && mock_len(1) == 2 && mock_len(2) == 1,
          "[byte-at-a-time] advanced buffer offset (no resend/skip)");

    /* 9. exact write stops immediately */
    mock_reset();
    mock_queue(2, 0);
    CHECK(send_all(42, "ab", 2) == 0, "[exact write] returns 0 without extra calls");
    CHECK(mock_calls() == 1, "[exact write] stops as soon as len is satisfied");
}

int main(void)
{
    run_send_suite();

    printf("\n%s: %d checks, %d failures\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
