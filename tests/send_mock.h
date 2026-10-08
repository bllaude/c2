#ifndef SEND_MOCK_H
#define SEND_MOCK_H

#include <sys/types.h>

typedef int (*send_all_fn)(int, const char *, size_t);

/* send_all() renamed per translation unit by the Makefile's `test` target
 * (-Dsend_all=agent_send_all / -Dsend_all=controller_send_all). */
int agent_send_all(int sock, const char *buf, size_t len);
int controller_send_all(int sock, const char *buf, size_t len);

/* Queue one scripted behaviour for the next mock send() call.
 * ret < 0 injects errno; ret >= 0 succeeds and sends ret bytes. */
void mock_queue(ssize_t ret, int err);
void mock_reset(void);
int  mock_calls(void);              /* number of send() calls so far        */
ssize_t mock_len(int call_index);   /* len argument passed on that call     */

#endif /* SEND_MOCK_H */