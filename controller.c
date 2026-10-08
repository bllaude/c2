// controller.c
//
// Operator side of a minimal remote-administration tool. Listens for an agent
// connection, authenticates it against a shared secret, then runs an
// interactive prompt: each line you type is executed on the managed host and
// its output is printed back.
//
// Usage:
//   controller [options]
//
// Options:
//   -p, --port PORT      TCP port to listen on (default DEFAULT_PORT)
//   -b, --bind ADDRESS   local address to bind (default: all interfaces)
//   -k, --key KEY        shared secret the agent must present
//       --no-auth        accept agents without requiring a secret
//                        (refuses to bind beyond loopback unless forced)
//       --allow-open     allow --no-auth on a non-loopback bind address
//   -c, --command CMD    run one command, print the result, then disconnect
//   -1, --one-shot       accept a single agent, then exit (default)
//       --multi          keep listening for further agents after a session
//   -t, --timeout SEC    per-response read timeout in seconds (0 = wait forever)
//   -v, --version        print version and exit
//   -h, --help           show this help
//
// Environment (overridden by command-line options):
//   CONTROLLER_PORT, CONTROLLER_BIND, CONTROLLER_KEY
//
// Exit codes: 0 normal end, 1 runtime/network error, 2 bad usage.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "protocol.h"

#define VERSION "1.0.0"

static void print_usage(const char *argv0) {
    printf(
        "usage: %s [options]\n"
        "  -p, --port PORT     TCP port to listen on (default %d)\n"
        "  -b, --bind ADDRESS  local address to bind (default: all)\n"
        "  -k, --key KEY       shared secret the agent must present\n"
        "      --no-auth       accept agents without a secret (loopback only)\n"
        "      --allow-open    allow --no-auth on a non-loopback address\n"
        "  -c, --command CMD   run one command, print the result, disconnect\n"
        "  -1, --one-shot      serve a single agent, then exit (default)\n"
        "      --multi         keep listening for further agents\n"
        "  -t, --timeout SEC   response timeout in seconds (0 = forever)\n"
        "  -v, --version       print version and exit\n"
        "  -h, --help          show this help\n"
        "(env: CONTROLLER_PORT, CONTROLLER_BIND, CONTROLLER_KEY)\n",
        argv0, DEFAULT_PORT);
}

/* Create, bind and listen. Returns the server socket or -1. */
static int make_listener(const char *bind_addr, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        perror("setsockopt"); /* non-fatal */

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (!bind_addr || !*bind_addr || strcmp(bind_addr, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
        fprintf(stderr, "error: invalid bind address '%s'\n", bind_addr);
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

/* True when `bind_addr` denotes a loopback interface. */
static int is_loopback_str(const char *bind_addr) {
    struct sockaddr_in probe;
    memset(&probe, 0, sizeof(probe));
    probe.sin_family = AF_INET;
    if (strcmp(bind_addr, "127.0.0.1") == 0 || strcmp(bind_addr, "localhost") == 0)
        return 1;
    if (inet_pton(AF_INET, bind_addr, &probe.sin_addr) == 1 &&
        ntohl(probe.sin_addr.s_addr) == INADDR_LOOPBACK)
        return 1;
    return 0;
}

/* Wait for `sock` to become readable within `timeout_sec` (0 = forever).
 * Returns 1 readable, 0 timeout, -1 error. */
static int wait_readable(int sock, int timeout_sec) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int r;
    do {
        r = select(sock + 1, &rfds, NULL, NULL, timeout_sec > 0 ? &tv : NULL);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
        perror("select");
        return -1;
    }
    return r == 0 ? 0 : 1;
}

/* Authenticate the newly accepted agent. Returns 0 when the session may start. */
static int authenticate(int sock, const char *key) {
    char line[LINE_MAX_LEN];

    /* 1. Announce our protocol version. */
    if (send_all(sock, PROTO_BANNER, strlen(PROTO_BANNER)) < 0)
        return -1;

    /* 2. Read the key the agent presents. */
    memset(line, 0, sizeof(line));
    if (proto_read_line(sock, line, sizeof(line)) < 0)
        return -1;
    proto_trim(line);

    if (strcmp(line, key) != 0) {
        fprintf(stderr, "[!] Agent presented a wrong or missing key\n");
        proto_write_line(sock, PROTO_REPLY_DENIED);
        return -1;
    }
    if (proto_write_line(sock, PROTO_REPLY_OK) < 0)
        return -1;
    return 0;
}

/* Send one command and print the framed reply. Returns 0 ok, -1 fatal. */
static int run_one(int sock, const char *cmd, int timeout_sec) {
    static char out[BUF_SIZE];

    if (proto_write_line(sock, cmd) < 0)
        return -1;

    int ready = wait_readable(sock, timeout_sec);
    if (ready == 0) {
        printf("[timeout] no response within %d s\n", timeout_sec);
        return 0;
    }
    if (ready < 0)
        return -1;

    memset(out, 0, sizeof(out));
    long n = proto_recv_frame(sock, out, sizeof(out));
    if (n == -2) {
        fprintf(stderr, "[!] Malformed frame from agent, dropping connection\n");
        return -1;
    }
    if (n < 0)
        return -1;
    if (n == 0)
        printf("(no output)\n");
    else
        printf("%s", out);
    fflush(stdout);
    return 0;
}

/* Interactive session with one authenticated agent. Returns 0 clean, -1 error. */
static int serve_agent(int sock, const char *one_shot_cmd, int timeout_sec) {
    char input[BUF_SIZE];

    if (one_shot_cmd)
        return run_one(sock, one_shot_cmd, timeout_sec);

    for (;;) {
        printf("controller> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) {
            /* EOF on stdin: politely shut the agent down. */
            proto_write_line(sock, CMD_EXIT);
            return 0;
        }
        proto_trim(input);
        if (input[0] == '\0')
            continue;
        if (strcmp(input, "quit") == 0 || strcmp(input, CMD_EXIT) == 0) {
            proto_write_line(sock, CMD_EXIT);
            return 0;
        }
        if (strcmp(input, "help") == 0) {
            printf("Type any shell command to run it on the managed host.\n"
                   "'quit' (or 'exit') shuts the agent session down.\n");
            continue;
        }
        if (run_one(sock, input, timeout_sec) < 0) {
            printf("[*] Connection to agent lost\n");
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    static const struct option long_opts[] = {
        {"port", required_argument, NULL, 'p'},
        {"bind", required_argument, NULL, 'b'},
        {"key", required_argument, NULL, 'k'},
        {"no-auth", no_argument, NULL, 0x100},
        {"allow-open", no_argument, NULL, 0x101},
        {"command", required_argument, NULL, 'c'},
        {"one-shot", no_argument, NULL, '1'},
        {"multi", no_argument, NULL, 0x102},
        {"timeout", required_argument, NULL, 't'},
        {"version", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    const char *bind_addr = getenv("CONTROLLER_BIND");
    const char *port_s = getenv("CONTROLLER_PORT");
    const char *key = getenv("CONTROLLER_KEY");
    const char *one_shot_cmd = NULL;
    int c, opt_index = 0;
    int multi = 0;
    int no_auth = 0;
    int allow_open = 0;
    int timeout_sec = 30;
    int rc_final = EXIT_SUCCESS;

    if (!bind_addr || !*bind_addr)
        bind_addr = "0.0.0.0";
    if (!key)
        key = "";

    while ((c = getopt_long(argc, argv, "p:b:k:c:t:1vh", long_opts, &opt_index)) != -1) {
        switch (c) {
        case 'p':
            port_s = optarg;
            break;
        case 'b':
            bind_addr = optarg;
            break;
        case 'k':
            key = optarg;
            break;
        case 'c':
            one_shot_cmd = optarg;
            break;
        case 't':
            timeout_sec = atoi(optarg);
            if (timeout_sec < 0)
                timeout_sec = 0;
            break;
        case '1':
            multi = 0;
            break;
        case 'v':
            printf("controller %s (protocol V1)\n", VERSION);
            return EXIT_SUCCESS;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        case 0x100: /* --no-auth */
            no_auth = 1;
            key = "";
            break;
        case 0x101: /* --allow-open */
            allow_open = 1;
            break;
        case 0x102: /* --multi */
            multi = 1;
            break;
        default:
            print_usage(argv[0]);
            return 2;
        }
    }

    if (strlen(key) >= MAX_KEY_LEN) {
        fprintf(stderr, "error: key too long (max %d characters)\n", MAX_KEY_LEN - 1);
        return 2;
    }
    if (timeout_sec < 0)
        timeout_sec = 0;

    int port = port_s ? atoi(port_s) : DEFAULT_PORT;
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "error: invalid port '%s'\n", port_s ? port_s : "");
        return 2;
    }

    /* Safety: unauthenticated operation is only sensible on loopback. */
    if (no_auth && !allow_open && !is_loopback_str(bind_addr)) {
        fprintf(stderr,
                "error: --no-auth refuses to bind to '%s' (not loopback).\n"
                "       Set a key with -k/--key, bind to 127.0.0.1, or use\n"
                "       --allow-open if you really know what you are doing.\n",
                bind_addr);
        return 2;
    }
    if (*key == '\0' && !no_auth) {
        fprintf(stderr,
                "warning: no shared secret configured (-k/--key or CONTROLLER_KEY);\n"
                "         only agents that also send an empty key will be accepted.\n");
    }

    signal(SIGPIPE, SIG_IGN); /* writes on a dropped peer return EPIPE */

    int server_fd = make_listener(bind_addr, port);
    if (server_fd < 0)
        return EXIT_FAILURE;

    printf("[+] Listening for agents on %s:%d (protocol V1)\n", bind_addr, port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int client_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            rc_final = EXIT_FAILURE;
            break;
        }

        char peerstr[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &peer.sin_addr, peerstr, sizeof(peerstr));
        printf("[+] Agent connected from %s:%u\n", peerstr, ntohs(peer.sin_port));
        fflush(stdout);

        if (authenticate(client_fd, key) == 0) {
            printf("[*] Authenticated. Type commands (Ctrl-D or 'quit' to end).\n");
            fflush(stdout);
            if (serve_agent(client_fd, one_shot_cmd, timeout_sec) < 0)
                rc_final = EXIT_FAILURE;
        }

        close(client_fd);
        if (!multi || one_shot_cmd)
            break;
        printf("[+] Waiting for the next agent...\n");
        fflush(stdout);
    }

    close(server_fd);
    return rc_final;
}