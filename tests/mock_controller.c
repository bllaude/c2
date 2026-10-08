/* tests/mock_controller.c
 * A minimal stand-in for the real controller used by the agent's end-to-end
 * test. It performs the V1 handshake, sends one command line, reads the
 * length-prefixed response frame, prints it between markers, then exits.
 *
 * Usage: mock_controller [-k KEY] [-p PORT] [COMMAND]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../protocol.h"

int main(int argc, char **argv)
{
    const char *cmd = "echo MOCK_RESPONSE";
    const char *key = "";
    int port = 4444;
    int opt;

    while ((opt = getopt(argc, argv, "k:p:h")) != -1) {
        switch (opt) {
        case 'k': key = optarg; break;
        case 'p': port = atoi(optarg); break;
        default:
            fprintf(stderr, "usage: %s [-k KEY] [-p PORT] [COMMAND]\n", argv[0]);
            return 2;
        }
    }
    if (optind < argc)
        cmd = argv[optind];

    int srv, cli, on = 1;
    struct sockaddr_in addr;

    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return EXIT_FAILURE; }
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return EXIT_FAILURE; }
    if (listen(srv, 1) < 0) { perror("listen"); return EXIT_FAILURE; }

    /* print so the harness knows we are ready */
    printf("MOCK_READY\n");
    fflush(stdout);

    cli = accept(srv, NULL, NULL);
    if (cli < 0) { perror("accept"); close(srv); return EXIT_FAILURE; }

    /* Handshake: banner, read key, verify, reply. */
    if (send_all(cli, PROTO_BANNER, strlen(PROTO_BANNER)) < 0) { perror("banner"); goto fail; }
    char got[LINE_MAX_LEN];
    if (proto_read_line(cli, got, sizeof(got)) < 0) { fprintf(stderr, "no key line\n"); goto fail; }
    proto_trim(got);
    if (strcmp(got, key) != 0) {
        proto_write_line(cli, PROTO_REPLY_DENIED);
        printf("MOCK_DENIED\n");
        fflush(stdout);
        goto fail;
    }
    if (proto_write_line(cli, PROTO_REPLY_OK) < 0) goto fail;

    /* Send the command line and read the framed response. */
    if (proto_write_line(cli, cmd) < 0) goto fail;

    char buf[BUF_SIZE];
    long n = proto_recv_frame(cli, buf, sizeof(buf));
    if (n >= 0) {
        printf("RESPONSE_BEGIN%sRESPONSE_END\n", buf);
    } else {
        printf("RESPONSE_EMPTY\n");
    }
    fflush(stdout);

    close(cli);
    close(srv);
    return EXIT_SUCCESS;

fail:
    close(cli);
    close(srv);
    return EXIT_FAILURE;
}
