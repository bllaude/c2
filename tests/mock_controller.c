/* tests/mock_controller.c
 * A minimal stand-in for the real controller used by the agent's unit test.
 * It listens on 127.0.0.1:4444, accepts one connection, sends a command,
 * prints the received response between markers, then closes the socket.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 4444
#define BUF_SIZE 4096

int main(int argc, char **argv)
{
    const char *cmd = (argc > 1) ? argv[1] : "echo MOCK_RESPONSE\n";
    int srv, cli, opt = 1;
    struct sockaddr_in addr;
    char buf[BUF_SIZE];

    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return EXIT_FAILURE; }
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return EXIT_FAILURE; }
    if (listen(srv, 1) < 0) { perror("listen"); return EXIT_FAILURE; }

    /* print so the harness knows we are ready */
    printf("MOCK_READY\n");
    fflush(stdout);

    cli = accept(srv, NULL, NULL);
    if (cli < 0) { perror("accept"); return EXIT_FAILURE; }

    if (send(cli, cmd, strlen(cmd), 0) < 0) { perror("send"); return EXIT_FAILURE; }

    memset(buf, 0, sizeof(buf));
    ssize_t n = recv(cli, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        printf("RESPONSE_BEGIN%sRESPONSE_END\n", buf);
    } else {
        printf("RESPONSE_EMPTY\n");
    }
    fflush(stdout);

    close(cli);
    close(srv);
    return EXIT_SUCCESS;
}