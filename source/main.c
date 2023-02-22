// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <wchar.h>

#include <mobile.h>
#include <mobile_inet.h>

#include "socket.h"
#include "bgblink.h"

struct mobile_user {
    pthread_mutex_t mutex_serial;
    pthread_mutex_t mutex_cond;
    pthread_cond_t cond;
    struct mobile_adapter *adapter;
    enum mobile_action action;
    FILE *config;
    _Atomic uint32_t bgb_clock;
    _Atomic uint32_t bgb_clock_latch[MOBILE_MAX_TIMERS];
    int sockets[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
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
        const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        memset(&u_addr->addr4, 0, sizeof(u_addr->addr4));
        u_addr->addr4.sin_family = AF_INET;
        u_addr->addr4.sin_port = htons(addr4->port);
        if (sizeof(struct in_addr) != sizeof(addr4->host)) return NULL;
        memcpy(&u_addr->addr4.sin_addr.s_addr, addr4->host,
            sizeof(struct in_addr));
        *addrlen = sizeof(struct sockaddr_in);
        return &u_addr->addr;
    } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
        const struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
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

static void impl_debug_log(void *user, const char *line)
{
    (void)user;
    fprintf(stderr, "%s\n", line);
}

static void impl_serial_disable(void *user)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    pthread_mutex_lock(&mobile->mutex_serial);
}

static void impl_serial_enable(void *user)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    pthread_mutex_unlock(&mobile->mutex_serial);
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    fseek(mobile->config, (long)offset, SEEK_SET);
    return fread(dest, 1, size, mobile->config) == size;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    fseek(mobile->config, (long)offset, SEEK_SET);
    return fwrite(src, 1, size, mobile->config) == size;
}

static void impl_time_latch(void *user, unsigned timer)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->bgb_clock_latch[timer] = mobile->bgb_clock;
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    return
        ((mobile->bgb_clock - mobile->bgb_clock_latch[timer]) & 0x7FFFFFFF) >=
        (uint32_t)((double)ms * (1 << 21) / 1000);
}

static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    assert(mobile->sockets[conn] == -1);

    int sock_socktype;
    switch (socktype) {
        case MOBILE_SOCKTYPE_TCP: sock_socktype = SOCK_STREAM; break;
        case MOBILE_SOCKTYPE_UDP: sock_socktype = SOCK_DGRAM; break;
        default: assert(false); return false;
    }

    int sock_addrtype;
    switch (addrtype) {
        case MOBILE_ADDRTYPE_IPV4: sock_addrtype = AF_INET; break;
        case MOBILE_ADDRTYPE_IPV6: sock_addrtype = AF_INET6; break;
        default: assert(false); return false;
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

    // Set SO_REUSEADDR so that we can bind to the same port again after
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
            (char *)&(int){1}, sizeof(int)) == -1) {
        socket_perror("setsockopt");
        socket_close(sock);
        return false;
    }

    // Set TCP_NODELAY to aid sending packets inmediately, reducing latency
    if (socktype == MOBILE_SOCKTYPE_TCP &&
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                (char *)&(int){1}, sizeof(int)) == -1) {
        socket_perror("setsockopt");
        socket_close(sock);
        return false;
    }

    int rc;
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

static void impl_sock_close(void *user, unsigned conn)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    assert(mobile->sockets[conn] != -1);
    socket_close(mobile->sockets[conn]);
    mobile->sockets[conn] = -1;
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];
    assert(sock != -1);

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
    fprintf(stderr, "Could not connect (ip %s port %s): ",
        sock_host, sock_port);
    socket_perror(NULL);
    return -1;
}

static bool impl_sock_listen(void *user, unsigned conn)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];
    assert(sock != -1);

    if (listen(sock, 1) == -1) {
        socket_perror("listen");
        return false;
    }

    return true;
}

static bool impl_sock_accept(void *user, unsigned conn)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];
    assert(sock != -1);

    if (socket_hasdata(sock, 0) <= 0) return false;
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

static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];
    assert(sock != -1);

    union u_sockaddr u_addr;
    socklen_t sock_addrlen;
    struct sockaddr *sock_addr = convert_sockaddr(&sock_addrlen, &u_addr, addr);

    ssize_t len = sendto(sock, data, size, 0, sock_addr, sock_addrlen);
    if (len == -1) {
        // If the socket is blocking, we just haven't sent anything
        int err = socket_geterror();
        if (err == SOCKET_EWOULDBLOCK) return 0;

        socket_perror("send");
        return -1;
    }
    return (int)len;
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    int sock = mobile->sockets[conn];
    assert(sock != -1);

    // Make sure at least one byte is in the buffer
    if (socket_hasdata(sock, 0) <= 0) return 0;

    union u_sockaddr u_addr = {0};
    socklen_t sock_addrlen = sizeof(u_addr);
    struct sockaddr *sock_addr = (struct sockaddr *)&u_addr;

    ssize_t len;
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
        if (err == SOCKET_EWOULDBLOCK) return 0;

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

    return (int)len;
}

static void update_title(struct mobile_user *mobile)
{
    wchar_t title[0x100];
    size_t i = 0;

    i += swprintf(title + i, (sizeof(title) / sizeof(*title)) - i,
        L"Mobile Adapter - ");

    if (mobile->number_peer[0]) {
        i += swprintf(title + i, (sizeof(title) / sizeof(*title)) - i,
            L"Call: %s", mobile->number_peer);
    } else {
        i += swprintf(title + i, (sizeof(title) / sizeof(*title)) - i,
            L"Disconnected");
    }

    if (mobile->number_user[0]) {
        i += swprintf(title + i, (sizeof(title) / sizeof(*title)) - i,
            L" (Your number: %s)", mobile->number_user);
    }

#if defined(__unix__)
    printf("\e]0;%ls\a", title);
    fflush(stdout);
#elif defined(__WIN32__)
    SetConsoleTitleW(title);
#endif
}

static void impl_update_number(void *user, enum mobile_number type, const char *number)
{
    struct mobile_user *mobile = (struct mobile_user *)user;

    char *dest = NULL;
    switch (type) {
        case MOBILE_NUMBER_USER: dest = mobile->number_user; break;
        case MOBILE_NUMBER_PEER: dest = mobile->number_peer; break;
        default: assert(false); return;
    }

    if (number) {
        strncpy(dest, number, MOBILE_MAX_NUMBER_SIZE);
        dest[MOBILE_MAX_NUMBER_SIZE] = '\0';
    } else {
        dest[0] = '\0';
    }

    update_title(mobile);
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

static void *thread_mobile_loop(void *user)
{
    struct mobile_user *mobile = (struct mobile_user *)user;
    for (;;) {
        // Implicitly unlocks mutex_cond while waiting
        pthread_cond_wait(&mobile->cond, &mobile->mutex_cond);

        // Process actions until we run out
        while (mobile->action != MOBILE_ACTION_NONE) {
            mobile_action_process(mobile->adapter, mobile->action);
            fflush(stdout);

            mobile->action = filter_actions(
                    mobile_action_get(mobile->adapter));
            if (mobile->action != MOBILE_ACTION_NONE) {
                // Sleep 10ms to avoid busylooping too hard
                nanosleep(&(struct timespec){.tv_nsec = 10000000}, NULL);
            }
        }
    }
    return NULL;
}

static void bgb_loop_action(struct mobile_user *mobile)
{
    // Called for every byte transfer, unlock thread_mobile_loop if there's
    //   anything to be done.

    // If the thread isn't doing anything, queue up the next action.
    if (pthread_mutex_trylock(&mobile->mutex_cond) != 0) return;
    if (mobile->action == MOBILE_ACTION_NONE) {
        enum mobile_action action = filter_actions(
                mobile_action_get(mobile->adapter));

        if (action != MOBILE_ACTION_NONE) {
            mobile->action = action;
            pthread_cond_signal(&mobile->cond);
        }
    }
    pthread_mutex_unlock(&mobile->mutex_cond);
}

static unsigned char bgb_loop_transfer(void *user, unsigned char c)
{
    // Transfer a byte over the serial port
    struct mobile_user *mobile = (struct mobile_user *)user;
    pthread_mutex_lock(&mobile->mutex_serial);
    c = mobile_transfer(mobile->adapter, c);
    pthread_mutex_unlock(&mobile->mutex_serial);
    bgb_loop_action(mobile);
    return c;
}

static void bgb_loop_timestamp(void *user, uint32_t t)
{
    // Update the timestamp sent by the emulator
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->bgb_clock = t;
    bgb_loop_action(mobile);
}

static bool signal_int_trig = false;
#if defined(__unix__)
static void signal_int(int signo)
{
    (void)signo;
    signal_int_trig = true;
}
#elif defined(__WIN32__)
static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    if (fdwCtrlType == CTRL_C_EVENT) {
        signal_int_trig = true;
        return TRUE;
    }
    return FALSE;
}
#endif

static char *program_name;

static void show_help(void)
{
    fprintf(stderr, "%s [-h] [-c config] [options] [bgb_host [bgb_port]]\n",
        program_name);
    exit(EXIT_FAILURE);
}

static void show_help_full(void)
{
    fprintf(stderr, "%s [-h] [-c config] [options] [bgb_host [bgb_port]]\n",
        program_name);
    fprintf(stderr, "\n"
        "-h|--help           Show this help\n"
        "-c|--config config  Config file path\n"
        "--device device     Adapter to emulate\n"
        "--unmetered         Signal unmetered communications to Pokémon\n"
        "--dns1 addr         Set DNS1 address override\n"
        "--dns2 addr         Set DNS2 address override\n"
        "--dns_port port     Set DNS port for address overrides\n"
        "--p2p_port port     Port to use for relay-less P2P communications\n"
        "--relay addr        Set relay server for P2P communications\n"
        "--relay-token hex   Set relay token (or empty to clear)\n"
    );
    exit(EXIT_SUCCESS);
}

static void main_checkparam(char *argv[])
{
    if (!argv[1]) {
        fprintf(stderr, "Missing parameter for %s\n", argv[0]);
        show_help();
    }
}

static void main_parse_addr(struct mobile_addr *dest, char *argv[])
{
    unsigned char ip[MOBILE_INET_PTON_MAXLEN];
    int rc = mobile_inet_pton(MOBILE_INET_PTON_ANY, argv[1], ip);

    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (rc) {
    case MOBILE_INET_PTON_IPV4:
        dest4->type = MOBILE_ADDRTYPE_IPV4;
        memcpy(dest4->host, ip, sizeof(dest4->host));
        break;
    case MOBILE_INET_PTON_IPV6:
        dest6->type = MOBILE_ADDRTYPE_IPV6;
        memcpy(dest6->host, ip, sizeof(dest6->host));
        break;
    default:
        fprintf(stderr, "Invalid parameter for %s: %s\n", argv[0], argv[1]);
        show_help();
    }
}

static void main_set_port(struct mobile_addr *dest, unsigned port)
{
    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (dest->type) {
    case MOBILE_ADDRTYPE_IPV4:
        dest4->port = port;
        break;
    case MOBILE_ADDRTYPE_IPV6:
        dest6->port = port;
        break;
    default:
        break;
    }
}

static bool main_parse_hex(unsigned char *buf, char *str, unsigned size)
{
    unsigned char x = 0;
    for (unsigned i = 0; i < size * 2; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
        else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
        else return false;

        x <<= 4;
        x |= c;

        if (i % 2 == 1) {
            buf[i / 2] = x;
            x = 0;
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    program_name = argv[0];
    setlocale(LC_ALL, "");

    char *host = "127.0.0.1";
    char *port = "8765";

    char *fname_config = "config.bin";
    enum mobile_adapter_device device = MOBILE_ADAPTER_BLUE;
    bool device_unmetered = false;
    struct mobile_addr dns1 = {0};
    struct mobile_addr dns2 = {0};
    unsigned dns_port = MOBILE_DNS_PORT;
    unsigned p2p_port = MOBILE_DEFAULT_P2P_PORT;
    struct mobile_addr relay = {0};
    bool relay_token_update = false;
    unsigned char *relay_token = NULL;
    unsigned char relay_token_buf[MOBILE_RELAY_TOKEN_SIZE];

    (void)argc;
    while (*++argv) {
        if ((*argv)[0] != '-') {
            break;
        } else if (strcmp(*argv, "--") == 0) {
            argv += 1;
            break;
        } else if (strcmp(*argv, "-h") == 0 || strcmp(*argv, "--help") == 0) {
            show_help_full();
        } else if (strcmp(*argv, "-c") == 0 || strcmp(*argv, "--config") == 0) {
            main_checkparam(argv);
            fname_config = argv[1];
            argv += 1;
        } else if (strcmp(*argv, "--device") == 0) {
            main_checkparam(argv);
            device = strtol(argv[1], NULL, 0);
            argv += 1;
        } else if (strcmp(*argv, "--unmetered") == 0) {
            device_unmetered = true;
        } else if (strcmp(*argv, "--dns1") == 0) {
            main_checkparam(argv);
            main_parse_addr(&dns1, argv);
            argv += 1;
        } else if (strcmp(*argv, "--dns2") == 0) {
            main_checkparam(argv);
            main_parse_addr(&dns2, argv);
            argv += 1;
        } else if (strcmp(*argv, "--dns_port") == 0) {
            main_checkparam(argv);
            char *endptr;
            dns_port = strtol(argv[1], &endptr, 10);
            if (!**argv || *endptr) {
                fprintf(stderr, "Invalid parameter for --dns_port: %s\n",
                    argv[1]);
                show_help();
            }
            argv += 1;
        } else if (strcmp(*argv, "--p2p_port") == 0) {
            main_checkparam(argv);
            p2p_port = strtol(argv[1], NULL, 0);
            argv += 1;
        } else if (strcmp(*argv, "--relay") == 0) {
            main_checkparam(argv);
            main_parse_addr(&relay, argv);
            main_set_port(&relay, MOBILE_DEFAULT_RELAY_PORT);
            argv += 1;
        } else if (strcmp(*argv, "--relay-token") == 0) {
            main_checkparam(argv);
            relay_token_update = true;
            bool ok = false;
            if (strlen(argv[1]) == 0) {
                ok = true;
                relay_token = NULL;
            } else if (strlen(argv[1]) == sizeof(relay_token_buf) * 2) {
                ok = main_parse_hex(relay_token_buf, argv[1],
                    sizeof(relay_token_buf));
                relay_token = relay_token_buf;
            }
            if (!ok) {
                fprintf(stderr, "Invalid parameter for --relay-token: %s\n",
                    argv[1]);
                show_help();
            }
            argv += 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", *argv);
            show_help();
        }
    }

    if (*argv) host = *argv++;
    if (*argv) port = *argv;

    // OS resources
    FILE *config = NULL;
    struct mobile_user *mobile = NULL;

    // Set the DNS ports
    main_set_port(&dns1, dns_port);
    main_set_port(&dns2, dns_port);

    // Open or create configuration
    config = fopen(fname_config, "r+b");
    if (!config) config = fopen(fname_config, "w+b");
    if (!config) {
        perror("fopen");
        goto error;
    }

    // Make sure config file is at least CONFIG_SIZE bytes big
    fseek(config, 0, SEEK_END);
    for (long i = ftell(config); i < MOBILE_CONFIG_SIZE; i++) {
        fputc(0, config);
    }
    rewind(config);

    // Initialize main data structure
    mobile = malloc(sizeof(struct mobile_user));
    if (!mobile) {
        perror("malloc");
        goto error;
    }
    mobile->mutex_serial = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    mobile->mutex_cond = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    mobile->cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    mobile->action = MOBILE_ACTION_NONE;
    mobile->config = config;
    mobile->bgb_clock = 0;
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++) mobile->bgb_clock_latch[i] = 0;
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++) mobile->sockets[i] = -1;
    mobile->number_user[0] = '\0';
    mobile->number_peer[0] = '\0';

    pthread_mutex_lock(&mobile->mutex_cond);
    pthread_mutex_lock(&mobile->mutex_serial);

    // Initialize mobile library
    mobile->adapter = mobile_new(mobile);
    mobile_def_debug_log(mobile->adapter, impl_debug_log);
    mobile_def_serial_disable(mobile->adapter, impl_serial_disable);
    mobile_def_serial_enable(mobile->adapter, impl_serial_enable);
    mobile_def_config_read(mobile->adapter, impl_config_read);
    mobile_def_config_write(mobile->adapter, impl_config_write);
    mobile_def_time_latch(mobile->adapter, impl_time_latch);
    mobile_def_time_check_ms(mobile->adapter, impl_time_check_ms);
    mobile_def_sock_open(mobile->adapter, impl_sock_open);
    mobile_def_sock_close(mobile->adapter, impl_sock_close);
    mobile_def_sock_connect(mobile->adapter, impl_sock_connect);
    mobile_def_sock_listen(mobile->adapter, impl_sock_listen);
    mobile_def_sock_accept(mobile->adapter, impl_sock_accept);
    mobile_def_sock_send(mobile->adapter, impl_sock_send);
    mobile_def_sock_recv(mobile->adapter, impl_sock_recv);
    mobile_def_update_number(mobile->adapter, impl_update_number);

    mobile_config_load(mobile->adapter);
    mobile_config_set_device(mobile->adapter, device, device_unmetered);
    mobile_config_set_dns(mobile->adapter, &dns1, &dns2);
    mobile_config_set_p2p_port(mobile->adapter, p2p_port);
    mobile_config_set_relay(mobile->adapter, &relay);
    if (relay_token_update) {
        mobile_config_set_relay_token(mobile->adapter, relay_token);
    }
    mobile_config_save(mobile->adapter);

    // Initialize windows sockets
#ifdef __WIN32__
    WSADATA wsaData;
    int wsa_err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_err != NO_ERROR) {
        fprintf(stderr, "WSAStartup failed with error: %d\n", wsa_err);
        goto error;
    }
#endif

    // Connect to the emulator
    int bgb_sock = socket_connect(host, port);
    if (bgb_sock == -1) {
        fprintf(stderr, "Could not connect (%s:%s): ", host, port);
        socket_perror(NULL);
        goto error;
    }
    if (setsockopt(bgb_sock, IPPROTO_TCP, TCP_NODELAY,
                (void *)&(int){1}, sizeof(int)) == -1) {
        socket_perror("setsockopt");
        goto error;
    }

    // Set up CTRL+C signal handler
#if defined(__unix__)
    if (sigaction(SIGINT, &(struct sigaction){.sa_handler = signal_int},
            NULL) == -1) {
        perror("sigaction");
        goto error;
    }
#elif defined(__WIN32__)
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        fprintf(stderr, "SetConsoleCtrlHandler failed\n");
        goto error;
    }
#endif
    update_title(mobile);

    // Start main mobile thread
    mobile_start(mobile->adapter);
    pthread_t mobile_thread;
    int pthread_err = pthread_create(&mobile_thread, NULL, thread_mobile_loop,
        mobile);
    if (pthread_err) {
        fprintf(stderr, "pthread_create: %s\n", strerror(pthread_err));
        goto error;
    }

    // Handle the emulator connection
    struct bgb_state bgb_state;
    if (bgb_init(&bgb_state, bgb_sock, bgb_loop_transfer, bgb_loop_timestamp,
            mobile)) {
        while (!signal_int_trig) if (!bgb_loop(&bgb_state)) break;
    }

    // Stop the main mobile thread
    pthread_cancel(mobile_thread);
    pthread_join(mobile_thread, NULL);
    mobile_stop(mobile->adapter);

    // Close all sockets
    for (unsigned i = 0; i < MOBILE_MAX_CONNECTIONS; i++) {
        if (mobile->sockets[i] != -1) socket_close(mobile->sockets[i]);
    }
    socket_close(bgb_sock);

#ifdef __WIN32__
    WSACleanup();
#endif
    free(mobile->adapter);
    free(mobile);
    fclose(config);

    return EXIT_SUCCESS;

error:
    if (mobile) {
        free(mobile->adapter);
        free(mobile);
    }
    if (config) fclose(config);
    return EXIT_FAILURE;
}
