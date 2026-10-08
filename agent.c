// agent.c
//
// Managed-host side of a minimal remote-administration tool. Run this only on
// machines you own or are explicitly authorised to administer.
//
// Usage:
//   agent [options] [<controller-host>] [<port>]
//
// Options:
//   -k, --key KEY      shared secret; must match the controller's key
//       --no-auth      start without a shared secret (testing / localhost only)
//   -r, --reconnect N  reconnect after a disconnect, N = max attempts
//                      (N omitted or 0 = keep trying forever)
//       --once         connect once and exit when the session ends (default)
//   -v, --version      print version and exit
//   -h, --help         show this help
//
// Environment (each value is overridden by the matching command-line option):
//   AGENT_HOST       controller address
//   AGENT_PORT       controller port
//   AGENT_KEY        shared secret
//   AGENT_RECONNECT  "1"/"true" enables unlimited retries
//
// The agent dials out to the controller, so no listening port is needed on the
// managed host. Commands arrive one line at a time and their output is sent
// back as a single length-prefixed frame (see protocol.h).
//
// Exit codes: 0 session ended normally, 1 runtime/network error, 2 bad usage.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "protocol.h"

#define VERSION "1.0.0"

static void print_usage(const char *argv0) {
    printf(
        "usage: %s [options] [<controller-host>] [<port>]\n"
        "  -k, --key KEY      shared secret required by the controller\n"
        "      --no-auth      skip the shared secret (localhost testing only)\n"
        "  -r, --reconnect N  reconnect after disconnects (N attempts, 0 = infinite)\n"
        "      --once         do not reconnect (default)\n"
        "  -v, --version      print version and exit\n"
        "  -h, --help         show this help\n"
        "defaults: host=%s port=%d  (env: AGENT_HOST, AGENT_PORT, AGENT_KEY, AGENT_RECONNECT)\n",
        argv0, DEFAULT_HOST, DEFAULT_PORT);
}

/* Perform the handshake described in protocol.h. Returns 0 on success. */
static int handshake(int sock, const char *key) {
    char line[LINE_MAX_LEN];

    /* 1. The controller identifies itself first. */
    if (proto_read_line(sock, line, sizeof(line)) != 0) {
        fprintf(stderr, "handshake: no banner from controller\n");
        return -1;
    }
    proto_trim(line);
    if (strcmp(line, "V1") != 0) {
        fprintf(stderr, "handshake: unsupported controller version '%s'\n", line);
        return -1;
    }

    /* 2. Prove we know the shared secret. Clear text -> trusted network or
     *    tunnelled only (see protocol.h). */
    if (proto_write_line(sock, key) < 0)
        return -1;

    memset(line, 0, sizeof(line));
    if (proto_read_line(sock, line, sizeof(line)) != 0)
        return -1;
    proto_trim(line);
    if (strcmp(line, PROTO_REPLY_OK) != 0) {
        if (strcmp(line, PROTO_REPLY_DENIED) == 0)
            fprintf(stderr, "handshake: controller rejected the key\n");
        else
            fprintf(stderr, "handshake: unexpected reply '%s'\n", line);
        return -1;
    }
    return 0;
}

/* Read the output of a shell command into buf, returning its length. */
static size_t run_command(const char *cmd, char *buf, size_t bufsz) {
    FILE *fp;
    size_t used = 0;

    buf[0] = '\0';
    fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        return (size_t)snprintf(buf, bufsz, "[error] cannot execute command\n");
    }

    while (used + 1 < bufsz) {
        size_t n = fread(buf + used, 1, bufsz - 1 - used, fp);
        if (n == 0)
            break;
        used += n;
    }
    buf[used] = '\0';
    if (ferror(fp))
        perror("fread");

    int status = pclose(fp);
    if (status != 0) {
        /* Append the exit status so the operator sees failed commands. */
        size_t remain = (used + 1 < bufsz) ? bufsz - used - 1 : 0;
        if (remain > 0)
            used += (size_t)snprintf(buf + used, remain, "[exit status %d]\n", status);
    }
    return used;
}

/* One connected session. Returns 0 for a normal end, -1 for an error. */
static int serve_session(int sock) {
    char cmd[BUF_SIZE];
    char result[BUF_SIZE];

    for (;;) {
        int rc;

        memset(cmd, 0, sizeof(cmd));
        rc = proto_read_line(sock, cmd, sizeof(cmd));
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[*] Lost contact with controller\n");
            return -1;
        }
        if (rc > 0)
            fprintf(stderr, "[!] Command truncated to %d bytes\n", BUF_SIZE - 1);

        proto_trim(cmd);
        if (cmd[0] == '\0')
            continue; /* keepalive / empty line */
        if (strcmp(cmd, CMD_EXIT) == 0) {
            printf("[*] Controller requested shutdown\n");
            fflush(stdout);
            return 0;
        }

        size_t len = run_command(cmd, result, sizeof(result));
        if (proto_send_frame(sock, result, len) < 0)
            return -1;
    }
}

int main(int argc, char **argv) {
    static const struct option long_opts[] = {
        {"key", required_argument, NULL, 'k'},
        {"no-auth", no_argument, NULL, 0x100},
        {"reconnect", optional_argument, NULL, 'r'},
        {"once", no_argument, NULL, 0x101},
        {"version", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    char hostbuf[256], portbuf[16];
    const char *host = getenv("AGENT_HOST");
    const char *port_s = getenv("AGENT_PORT");
    const char *key = getenv("AGENT_KEY");
    const char *env_r;
    int c, opt_index = 0;
    int max_reconnects = 0; /* 0 = single attempt */

    if (!host || !*host)
        host = DEFAULT_HOST;
    if (!port_s || !*port_s)
        port_s = NULL;
    if (!key)
        key = "";

    env_r = getenv("AGENT_RECONNECT");
    if (env_r && (*env_r == '1' || strcasecmp(env_r, "true") == 0))
        max_reconnects = -1; /* infinite */

    while ((c = getopt_long(argc, argv, "k:r::vh", long_opts, &opt_index)) != -1) {
        switch (c) {
        case 'k':
            key = optarg;
            break;
        case 'r':
            max_reconnects = optarg ? atoi(optarg) : 0; /* 0 = infinite */
            break;
        case 'v':
            printf("agent %s (protocol V1)\n", VERSION);
            return EXIT_SUCCESS;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        case 0x100: /* --no-auth */
            key = "";
            break;
        case 0x101: /* --once */
            max_reconnects = 0;
            break;
        default:
            print_usage(argv[0]);
            return 2;
        }
    }

    if (optind < argc) {
        snprintf(hostbuf, sizeof(hostbuf), "%s", argv[optind]);
        host = hostbuf;
        if (optind + 1 < argc) {
            snprintf(portbuf, sizeof(portbuf), "%s", argv[optind + 1]);
            port_s = portbuf;
        }
    }

    if (strlen(key) >= MAX_KEY_LEN) {
        fprintf(stderr, "error: key too long (max %d characters)\n", MAX_KEY_LEN - 1);
        return 2;
    }
    if (*key == '\0') {
        fprintf(stderr,
                "warning: running with NO shared secret; anyone who can reach the\n"
                "         controller can drive this host. Use -k/--key or AGENT_KEY.\n");
    }

    int port = port_s ? atoi(port_s) : DEFAULT_PORT;
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "error: invalid port '%s'\n", port_s ? port_s : "");
        return 2;
    }

    snprintf(hostbuf, sizeof(hostbuf), "%s", host);

    struct addrinfo hints, *res0 = NULL;
    char portstr[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; /* IPv4 and IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    int gai = getaddrinfo(hostbuf, portstr, &hints, &res0);
    if (gai != 0) {
        fprintf(stderr, "error: cannot resolve controller '%s': %s\n",
                hostbuf, gai_strerror(gai));
        return 1;
    }

    int rc = EXIT_FAILURE;
    int attempt = 0;
    for (;;) {
        int sock = -1;
        struct addrinfo *rp;

        for (rp = res0; rp; rp = rp->ai_next) {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock < 0)
                continue;
            if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
                break;
            close(sock);
            sock = -1;
        }

        if (sock < 0) {
            perror("connect");
            rc = EXIT_FAILURE;
        } else {
            printf("[*] Connected to controller %s:%d\n", hostbuf, port);
            fflush(stdout);
            if (handshake(sock, key) != 0) {
                rc = EXIT_FAILURE;
            } else {
                printf("[*] Authenticated, waiting for commands\n");
                fflush(stdout);
                rc = (serve_session(sock) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
            }
            close(sock);
        }

        if (max_reconnects == 0)
            break;
        attempt++;
        if (max_reconnects > 0 && attempt >= max_reconnects) {
            fprintf(stderr, "[*] Giving up after %d attempt(s)\n", attempt);
            break;
        }
        unsigned int delay = 1u << (attempt > 5 ? 5 : attempt); /* capped ~32 s */
        if (delay > 60)
            delay = 60;
        printf("[*] Reconnecting in %u s (attempt %d)\n", delay, attempt + 1);
        fflush(stdout);
        sleep(delay);
    }

    freeaddrinfo(res0);
    return rc;
}