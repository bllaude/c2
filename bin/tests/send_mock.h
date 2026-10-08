#ifndef SEND_MOCK_H
#define SEND_MOCK_H

#include <sys/types.h>

/* Queue one scripted behaviour for the next mock send() call.
 * ret < 0 injects errno; ret >= 0 succeeds and sends ret bytes. */
void mock_queue(ssize_t ret, int err);
void mock_reset(void);
int  mock_calls(void);              /* number of send() calls so far        */
ssize_t mock_len(int call_index);   /* len argument passed on that call     */

#endif /* SEND_MOCK_H */