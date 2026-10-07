/* tests/send_mock.c - controllable stand-in for libc send().
 * Linked via -Wl,--wrap=send (see Makefile `test` target). */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>

#define MOCK_MAX_CALLS 16

static ssize_t g_ret[MOCK_MAX_CALLS];     /* scripted return values */
static int     g_err[MOCK_MAX_CALLS];     /* scripted errno values  */
static size_t  g_len_arg[MOCK_MAX_CALLS]; /* len seen per call      */
static int     g_queued;                  /* behaviours queued      */
static int     g_calls;                   /* calls observed         */

void mock_queue(ssize_t ret, int err)
{
    if (g_queued < MOCK_MAX_CALLS) {
        g_ret[g_queued] = ret;
        g_err[g_queued] = err;
        g_queued++;
    }
}

void mock_reset(void)
{
    g_queued = 0;
    g_calls = 0;
}

int mock_calls(void) { return g_calls; }

ssize_t mock_len(int call_index)
{
    return (call_index >= 0 && call_index < g_calls)
               ? (ssize_t)g_len_arg[call_index] : -1;
}

ssize_t __wrap_send(int sock, const void *buf, size_t len, int flags)
{
    (void)sock; (void)buf; (void)flags;
    if (g_calls >= MOCK_MAX_CALLS)
        return -1; /* safety: fail rather than crash */

    g_len_arg[g_calls] = len;
    ssize_t r = (g_calls < g_queued) ? g_ret[g_calls] : 0;
    int e     = (g_calls < g_queued) ? g_err[g_calls] : 0;
    g_calls++;

    if (r < 0 && e != 0)
        errno = e;
    return r;
}