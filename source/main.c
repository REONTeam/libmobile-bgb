// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <locale.h>
#include <pthread.h>
#include "libmobile/mobile.h"
#include "libmobile/inet_pton.h"

#include "socket.h"
#include "bgblink.h"

#include "libmobile/debug_cmd.h"

struct mobile_user {
    pthread_mutex_t mutex_serial;
    pthread_mutex_t mutex_cond;
    pthread_cond_t cond;
    struct mobile_adapter adapter;
    enum mobile_action action;
    FILE *config;
    _Atomic uint32_t bgb_clock;
    _Atomic uint32_t bgb_clock_latch[MOBILE_MAX_TIMERS];
    int sockets[MOBILE_MAX_CONNECTIONS];
};

union u_sockaddr {
    struct sockaddr addr;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
};

static struct sockaddr *convert_sockaddr(socklen_t *addrlen, union u_sockaddr *u_addr, const struct mobile_addr *addr)
{
    if (!addr) {
        *addrlen = 0;
        return NULL;
    } else if (addr->type == MOBILE_ADDRTYPE_IPV4) {
        struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        memset(&u_addr->addr4, 0, sizeof(u_addr->addr4));
        u_addr->addr4.sin_family = AF_INET;
        u_addr->addr4.sin_port = htons(addr4->port);
        if (sizeof(struct in_addr) != sizeof(addr4->host)) return NULL;
        memcpy(&u_addr->addr4.sin_addr.s_addr, addr4->host,
            sizeof(struct in_addr));
        *addrlen = sizeof(struct sockaddr_in);
        return &u_addr->addr;
    } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
        struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
        memset(&u_addr->addr6, 0, sizeof(u_addr->addr6));
        u_addr->addr6.sin6_family = AF_INET6;
        u_addr->addr6.sin6_port = htons(addr6->port);
        if (sizeof(struct in6_addr) != sizeof(addr6->host)) return NULL;
        memcpy(&u_addr->addr6.sin6_addr.s6_addr, addr6->host,
            sizeof(struct in6_addr));
        *addrlen = sizeof(struct sockaddr_in6);
        return &u_addr->addr;
    } else {
        *addrlen = 0;
        return NULL;
    }
}

void mobile_board_serial_disable(void *user)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    pthread_mutex_lock(&mobile->mutex_serial);
}

void mobile_board_serial_enable(void *user)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    pthread_mutex_unlock(&mobile->mutex_serial);
}

bool mobile_board_config_read(void *user, void *dest, const uintptr_t offset, const size_t size)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    fseek(mobile->config, offset, SEEK_SET);
    return fread(dest, 1, size, mobile->config) == size;
}

bool mobile_board_config_write(void *user, const void *src, const uintptr_t offset, const size_t size)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    fseek(mobile->config, offset, SEEK_SET);
    return fwrite(src, 1, size, mobile->config) == size;
}

void mobile_board_time_latch(void *user, enum mobile_timers timer)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->bgb_clock_latch[timer] = mobile->bgb_clock;
}

bool mobile_board_time_check_ms(void *user, enum mobile_timers timer, unsigned ms)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    return
        ((mobile->bgb_clock - mobile->bgb_clock_latch[timer]) & 0x7FFFFFFF) >=
        (uint32_t)((double)ms * (1 << 21) / 1000);
}

bool mobile_board_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    assert(mobile->sockets[conn] == -1);

    int sock_socktype;
    switch (socktype) {
        case MOBILE_SOCKTYPE_TCP: sock_socktype = SOCK_STREAM; break;
        case MOBILE_SOCKTYPE_UDP: sock_socktype = SOCK_DGRAM; break;
        default: return false;
    }

    int sock_addrtype;
    switch (addrtype) {
        case MOBILE_ADDRTYPE_IPV4: sock_addrtype = AF_INET; break;
        case MOBILE_ADDRTYPE_IPV6: sock_addrtype = AF_INET6; break;
        default: return false;
    }

    int sock = socket(sock_addrtype, sock_socktype, 0);
    if (sock == -1) {
        socket_perror("socket");
        return false;
    }
    if (socket_setblocking(sock, 0) == -1) {
        socket_close(sock);
        return false;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
            (char *)&(int){1}, sizeof(int)) == -1) {
        socket_perror("setsockopt");
        socket_close(sock);
        return false;
    }

    int rc = -1;
    if (addrtype == MOBILE_ADDRTYPE_IPV4) {
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(bindport),
        };
        rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    } else {
        struct sockaddr_in6 addr = {
            .sin6_family = AF_INET6,
            .sin6_port = htons(bindport),
        };
        rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    }
    if (rc == -1) {
        socket_perror("bind");
        socket_close(sock);
        return false;
    }

    mobile->sockets[conn] = sock;
    return true;
}

void mobile_board_sock_close(void *user, unsigned conn)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    assert(mobile->sockets[conn] != -1);
    socket_close(mobile->sockets[conn]);
    mobile->sockets[conn] = -1;
}

int mobile_board_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];

    union u_sockaddr u_addr;
    socklen_t sock_addrlen;
    struct sockaddr *sock_addr = convert_sockaddr(&sock_addrlen, &u_addr, addr);

    // Try to connect/check if we're connected
    if (connect(sock, sock_addr, sock_addrlen) != -1) return 1;
    int err = socket_geterror();

    // If the connection is in progress, block at most 100ms to see if it's
    //   enough for it to connect.
    if (err == SOCKET_EWOULDBLOCK || err == SOCKET_EINPROGRESS
            || err == SOCKET_EALREADY) {
        int rc = socket_isconnected(sock, 100);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        err = socket_geterror();
    }

    char sock_host[INET6_ADDRSTRLEN] = {0};
    char sock_port[6] = {0};
    socket_straddr(sock_host, sizeof(sock_host), sock_port, sock_addr,
        sock_addrlen);
    socket_seterror(err);
    fprintf(stderr, "Could not connect (ip %s port %s):",
        sock_host, sock_port);
    socket_perror(NULL);
    return -1;
}

bool mobile_board_sock_listen(void *user, unsigned conn)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];

    if (listen(sock, 1) == -1) {
        socket_perror("listen");
        return false;
    }

    return true;
}

bool mobile_board_sock_accept(void *user, unsigned conn)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];

    if (socket_hasdata(sock, 1000) <= 0) return false;
    int newsock = accept(sock, NULL, NULL);
    if (newsock == -1) {
        socket_perror("accept");
        return false;
    }
    if (socket_setblocking(newsock, 0) == -1) return false;

    socket_close(sock);
    mobile->sockets[conn] = newsock;
    return true;
}

int mobile_board_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];

    union u_sockaddr u_addr;
    socklen_t sock_addrlen;
    struct sockaddr *sock_addr = convert_sockaddr(&sock_addrlen, &u_addr, addr);

    ssize_t len = sendto(sock, data, size, 0, sock_addr, sock_addrlen);
    if (len == -1) {
        // If the socket is blocking, we just haven't sent anything
        int err = socket_geterror();
        if (err == SOCKET_EWOULDBLOCK || err == SOCKET_EAGAIN) return 0;

        socket_perror("send");
        return -1;
    }
    return len;
}

int mobile_board_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];

    // Make sure at least one byte is in the buffer
    if (socket_hasdata(sock, 0) <= 0) return 0;

    union u_sockaddr u_addr = {0};
    socklen_t sock_addrlen = sizeof(u_addr);
    struct sockaddr *sock_addr = (struct sockaddr *)&u_addr;

    ssize_t len = 0;
    if (data) {
        // Retrieve at least 1 byte from the buffer
        len = recvfrom(sock, data, size, 0, sock_addr, &sock_addrlen);
    } else {
        // Check if at least 1 byte is available in buffer
        char c;
        len = recvfrom(sock, &c, 1, MSG_PEEK, sock_addr, &sock_addrlen);
    }
    if (len == -1) {
        // If the socket is blocking, we just haven't received anything
        // Though this shouldn't happen thanks to the socket_hasdata check.
        int err = socket_geterror();
        if (err == SOCKET_EWOULDBLOCK || err == SOCKET_EAGAIN) return 0;

        socket_perror("recv");
        return -1;
    }

    // A length of 0 will be returned if the remote has disconnected.
    if (len == 0) {
        // Though it's only relevant to TCP sockets, as UDP sockets may receive
        // zero-length datagrams.
        int sock_type = 0;
        socklen_t sock_type_len = sizeof(sock_type);
        getsockopt(sock, SOL_SOCKET, SO_TYPE, (char *)&sock_type,
            &sock_type_len);
        if (sock_type == SOCK_STREAM) return -2;
    }

    if (!data) return 0;

    if (addr && sock_addrlen) {
        if (sock_addr->sa_family == AF_INET) {
            struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
            addr4->type = MOBILE_ADDRTYPE_IPV4;
            addr4->port = ntohs(u_addr.addr4.sin_port);
            memcpy(addr4->host, &u_addr.addr4.sin_addr.s_addr,
                sizeof(addr4->host));
        } else if (sock_addr->sa_family == AF_INET6) {
            struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
            addr6->type = MOBILE_ADDRTYPE_IPV6;
            addr6->port = ntohs(u_addr.addr6.sin6_port);
            memcpy(addr6->host, &u_addr.addr6.sin6_addr.s6_addr,
                sizeof(addr6->host));
        }
    }

    return len;
}

static enum mobile_action filter_actions(enum mobile_action action)
{
    // Turns actions that aren't relevant to the emulator into
    //   MOBILE_ACTION_NONE

    switch (action) {
    // In an emulator, serial can't desync
    case MOBILE_ACTION_RESET_SERIAL:
        return MOBILE_ACTION_NONE;

    default:
        return action;
    }
}

void *thread_mobile_loop(void *user)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    pthread_mutex_lock(&mobile->mutex_cond);
    for (;;) {
        // Implicitly unlocks mutex_cond while waiting
        pthread_cond_wait(&mobile->cond, &mobile->mutex_cond);

        // Process actions until we run out
        while (mobile->action != MOBILE_ACTION_NONE) {
            mobile_action_process(&mobile->adapter, mobile->action);
            fflush(stdout);

            mobile->action = filter_actions(
                    mobile_action_get(&mobile->adapter));
        }
    }
    pthread_mutex_unlock(&mobile->mutex_cond);
}

void bgb_loop_action(struct mobile_user *mobile)
{
    // Called for every byte transfer, unlock thread_mobile_loop if there's
    //   anything to be done.

    // If the thread isn't doing anything, queue up the next action.
    if (pthread_mutex_trylock(&mobile->mutex_cond) != 0) return;
    if (mobile->action == MOBILE_ACTION_NONE) {
        enum mobile_action action = filter_actions(
                mobile_action_get(&mobile->adapter));

        if (action != MOBILE_ACTION_NONE) {
            mobile->action = action;
            pthread_cond_signal(&mobile->cond);
        }
    }
    pthread_mutex_unlock(&mobile->mutex_cond);
}

unsigned char bgb_loop_transfer(void *user, unsigned char c)
{
    // Transfer a byte over the serial port
    struct mobile_user *mobile = (struct mobile_user *)user;
    pthread_mutex_lock(&mobile->mutex_serial);
    c = mobile_transfer(&mobile->adapter, c);
    pthread_mutex_unlock(&mobile->mutex_serial);
    bgb_loop_action(mobile);
    return c;
}

void bgb_loop_timestamp(void *user, uint32_t t)
{
    // Update the timestamp sent by the emulator
    struct mobile_user *mobile = (struct mobile_user *)user;

    // Attempt to detect the clock going back in time
    // This is probably a BGB bug, caused by enabling some options, such as
    //   the "break on ld d,d" option.
    if (((mobile->bgb_clock - t) & 0x7FFFFFFF) < 0x100) {
        fprintf(stderr, "[BUG] Emulator went back in time? "
                "old: 0x%08X; new: 0x%08X\n", mobile->bgb_clock, t);
        return;
    }

    mobile->bgb_clock = t;
    bgb_loop_action(mobile);
}

char *program_name;

void show_help(void)
{
    fprintf(stderr, "%s [-h] [-c config] [host [port]]\n", program_name);
    exit(EXIT_FAILURE);
}

void show_help_full(void)
{
    fprintf(stderr, "%s [-h] [-c config] [host [port]]\n", program_name);
    fprintf(stderr, "\n"
        "-h|--help           Show this help\n"
        "-c|--config config  Config file path\n"
        "--dns1 addr         Set DNS1 address override\n"
        "--dns2 addr         Set DNS2 address override\n"
        "--p2p_port port     Port to use for p2p communications\n"
        "--device device     Adapter to emulate\n"
        "--unmetered         Signal unmetered communications to Pokémon\n"
    );
    exit(EXIT_SUCCESS);
}

void main_checkparam(char *argv[])
{
    if (!argv[1]) {
        fprintf(stderr, "Missing parameter: %s\n", argv[0]);
        show_help();
    }
}

void main_parse_addr(struct mobile_addr *dest, char *argv[])
{
    unsigned char ip[MOBILE_PTON_MAXLEN];
    int rc = mobile_pton(MOBILE_PTON_ANY, argv[1], ip);

    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (rc) {
    case MOBILE_PTON_IPV4:
        dest4->type = MOBILE_ADDRTYPE_IPV4;
        dest4->port = MOBILE_DNS_PORT;
        memcpy(dest4->host, ip, sizeof(dest4->host));
        break;
    case MOBILE_PTON_IPV6:
        dest6->type = MOBILE_ADDRTYPE_IPV6;
        dest6->port = MOBILE_DNS_PORT;
        memcpy(dest6->host, ip, sizeof(dest6->host));
        break;
    default:
        fprintf(stderr, "Invalid parameter for %s: %s\n", argv[0], argv[1]);
        show_help();
    }
}

int main(int argc, char *argv[])
{
    program_name = argv[0];
    setlocale(LC_ALL, "");

    char *host = "127.0.0.1";
    char *port = "8765";

    char *fname_config = "config.bin";

    struct mobile_adapter_config adapter_config = MOBILE_ADAPTER_CONFIG_DEFAULT;

    (void)argc;
    while (*++argv) {
        if ((*argv)[0] != '-' || strcmp(*argv, "--") == 0) {
            break;
        } else if (strcmp(*argv, "-h") == 0 || strcmp(*argv, "--help") == 0) {
            show_help_full();
        } else if (strcmp(*argv, "-c") == 0 || strcmp(*argv, "--config") == 0) {
            main_checkparam(argv);
            fname_config = argv[1];
            argv += 1;
        } else if (strcmp(*argv, "--dns1") == 0) {
            main_checkparam(argv);
            main_parse_addr(&adapter_config.dns1, argv);
            argv += 1;
        } else if (strcmp(*argv, "--dns2") == 0) {
            main_checkparam(argv);
            main_parse_addr(&adapter_config.dns2, argv);
            argv += 1;
        } else if (strcmp(*argv, "--p2p_port") == 0) {
            main_checkparam(argv);
            adapter_config.p2p_port = strtol(argv[1], NULL, 0);
            argv += 1;
        } else if (strcmp(*argv, "--device") == 0) {
            main_checkparam(argv);
            adapter_config.device = strtol(argv[1], NULL, 0);
            argv += 1;
        } else if (strcmp(*argv, "--unmetered") == 0) {
            adapter_config.unmetered = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", *argv);
            show_help();
        }
    }

    if (*argv) host = *argv++;
    if (*argv) port = *argv++;

    FILE *config = fopen(fname_config, "r+b");
    if (!config) config = fopen(fname_config, "w+b");
    if (!config) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    // Make sure config file is at least MOBILE_CONFIG_SIZE bytes big
    fseek(config, 0, SEEK_END);
    for (int i = ftell(config); i < MOBILE_CONFIG_SIZE; i++) {
        fputc(0, config);
    }
    rewind(config);

#ifdef __WIN32__
    WSADATA wsaData;
    int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err != NO_ERROR) {
        printf("WSAStartup failed with error: %d\n", err);
        return EXIT_FAILURE;
    }
#endif

    int bgb_sock = socket_connect(host, port);
    if (bgb_sock == -1) {
        fprintf(stderr, "Could not connect (%s:%s):", host, port);
        socket_perror(NULL);
        return EXIT_FAILURE;
    }

    if (setsockopt(bgb_sock, IPPROTO_TCP, TCP_NODELAY, (void *)&(int){1}, sizeof(int)) == -1) {
        socket_perror("setsockopt");
        return EXIT_FAILURE;
    }

    struct mobile_user *mobile = malloc(sizeof(struct mobile_user));
    if (!mobile) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    mobile->mutex_serial = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    mobile->mutex_cond = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    mobile->cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    mobile->action = MOBILE_ACTION_NONE;
    mobile->config = config;
    mobile->bgb_clock = 0;
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++) mobile->bgb_clock_latch[i] = 0;
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++) mobile->sockets[i] = -1;
    mobile_init(&mobile->adapter, mobile, &adapter_config);

    pthread_t mobile_thread;
    int pthread_err = pthread_create(&mobile_thread, NULL, thread_mobile_loop, mobile);
    if (pthread_err) {
        fprintf(stderr, "pthread_create: %s\n", strerror(pthread_err));
        return EXIT_FAILURE;
    }
    bgb_loop(bgb_sock, bgb_loop_transfer, bgb_loop_timestamp, mobile);
    pthread_cancel(mobile_thread);
    pthread_join(mobile_thread, NULL);

    for (unsigned i = 0; i < MOBILE_MAX_CONNECTIONS; i++) {
        if (mobile->sockets[i] != -1) socket_close(mobile->sockets[i]);
    }
    socket_close(bgb_sock);

#ifdef __WIN32__
    WSACleanup();
#endif

    fclose(mobile->config);
    free(mobile);

    return EXIT_SUCCESS;
}
