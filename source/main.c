// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <locale.h>
#include <signal.h>
#include <wchar.h>

#include <mobile.h>
#include <mobile_inet.h>

#include "bgblink.h"
#include "socket.h"
#include "socket_impl.h"

struct mobile_user {
    struct mobile_adapter *adapter;
    struct socket_impl socket;
    enum mobile_action action;
    FILE *config;
    volatile bool reset;
    volatile uint32_t bgb_clock;
    bool bgb_clock_init;
    uint32_t bgb_clock_latch[MOBILE_MAX_TIMERS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
};

static void impl_debug_log(void *user, const char *line)
{
    (void)user;
    fprintf(stderr, "%s\n", line);
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size)
{
    struct mobile_user *mobile = user;
    fseek(mobile->config, (long)offset, SEEK_SET);
    return fread(dest, 1, size, mobile->config) == size;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size)
{
    struct mobile_user *mobile = user;
    fseek(mobile->config, (long)offset, SEEK_SET);
    return fwrite(src, 1, size, mobile->config) == size;
}

static void impl_time_latch(void *user, unsigned timer)
{
    struct mobile_user *mobile = user;
    mobile->bgb_clock_latch[timer] = mobile->bgb_clock;
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms)
{
    struct mobile_user *mobile = user;
    return
        ((mobile->bgb_clock - mobile->bgb_clock_latch[timer]) & 0x7FFFFFFF) >=
        (uint32_t)((double)ms * (1 << 21) / 1000);
}

static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype type, enum mobile_addrtype addrtype, unsigned bindport)
{
    struct mobile_user *mobile = user;
    return socket_impl_open(&mobile->socket, conn, type, addrtype, bindport);
}

static void impl_sock_close(void *user, unsigned conn)
{
    struct mobile_user *mobile = user;
    socket_impl_close(&mobile->socket, conn);
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr)
{
    struct mobile_user *mobile = user;
    return socket_impl_connect(&mobile->socket, conn, addr);
}

static bool impl_sock_listen(void *user, unsigned conn)
{
    struct mobile_user *mobile = user;
    return socket_impl_listen(&mobile->socket, conn);
}

static bool impl_sock_accept(void *user, unsigned conn)
{
    struct mobile_user *mobile = user;
    return socket_impl_accept(&mobile->socket, conn);
}

static int impl_sock_send(void *user, unsigned conn, const void *data, unsigned size, const struct mobile_addr *addr)
{
    struct mobile_user *mobile = user;
    return socket_impl_send(&mobile->socket, conn, data, size, addr);
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr)
{
    struct mobile_user *mobile = user;
    return socket_impl_recv(&mobile->socket, conn, data, size, addr);
}

static void update_title(struct mobile_user *mobile)
{
    wchar_t title[0x100];
    size_t i = 0;

    i += swprintf(title + i, (sizeof(title) / sizeof(*title)) - i,
        L"Mobile Adapter - ");

    if (mobile->number_peer[0]) {
        i += swprintf(title + i, (sizeof(title) / sizeof(*title)) - i,
            L"Call: %hs", mobile->number_peer);
    } else {
        i += swprintf(title + i, (sizeof(title) / sizeof(*title)) - i,
            L"Disconnected");
    }

    if (mobile->number_user[0]) {
        i += swprintf(title + i, (sizeof(title) / sizeof(*title)) - i,
            L" (Your number: %hs)", mobile->number_user);
    }

#if defined(__unix__)
    printf("\e]0;%ls\a", title);
    fflush(stdout);
#elif defined(_WIN32)
    SetConsoleTitleW(title);
#endif
}

static void impl_update_number(void *user, enum mobile_number type, const char *number)
{
    struct mobile_user *mobile = user;

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

static volatile bool signal_int_trig = false;
static void signal_int(int signo)
{
    (void)signo;
    signal_int_trig = true;
}
#ifdef _WIN32
static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    if (fdwCtrlType == CTRL_C_EVENT ||
            fdwCtrlType == CTRL_CLOSE_EVENT) {
        signal_int(SIGINT);
        return TRUE;
    }
    return FALSE;
}
#endif

static enum mobile_action filter_actions(enum mobile_action actions)
{
    // Filter out the actions that aren't relevant to this emulator
    return actions & ~MOBILE_ACTION_RESET_SERIAL;
}

static bool mobile_handle_loop(struct mobile_user *mobile)
{
    // Reset the adapter if requested
    if (mobile->reset) {
        mobile_stop(mobile->adapter);
        mobile_start(mobile->adapter);
        mobile->reset = false;
    }

    // Fetch action if none exists
    if (mobile->action == MOBILE_ACTION_NONE) {
        mobile->action =
            filter_actions(mobile_actions_get(mobile->adapter));
    }

    // Process action
    if (mobile->action != MOBILE_ACTION_NONE) {
        mobile_actions_process(mobile->adapter, mobile->action);

        // Fetch next action
        mobile->action =
            filter_actions(mobile_actions_get(mobile->adapter));
    }
    return true;
}

static unsigned char bgb_loop_transfer(void *user, unsigned char c)
{
    // Transfer a byte over the serial port
    struct mobile_user *mobile = user;
    c = mobile_transfer(mobile->adapter, c);
    return c;
}

static void bgb_loop_timestamp(void *user, uint32_t t)
{
    // Update the timestamp sent by the emulator
    struct mobile_user *mobile = user;

    // Bail if the time difference is too big. This happens whenever the
    //   emulator is reset, a new game is loaded, or a save state is loaded.
    uint32_t diff = (t - mobile->bgb_clock) & 0x7FFFFFFF;
    if (diff > 0x1000) {
        fprintf(stderr, "[BGB] Emulator reset detected! Resetting adapter\n");
        mobile->reset = true;
    }

    mobile->bgb_clock = t;
}

static void bgb_loop_timestamp_init(void *user, uint32_t t)
{
    // Initialize the clock
    struct mobile_user *mobile = user;
    mobile->bgb_clock = t;
    mobile->bgb_clock_init = true;
}

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
    mobile->action = MOBILE_ACTION_NONE;
    mobile->config = config;
    mobile->reset = false;
    mobile->bgb_clock = 0;
    mobile->bgb_clock_init = false;
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++) mobile->bgb_clock_latch[i] = 0;
    mobile->number_user[0] = '\0';
    mobile->number_peer[0] = '\0';
    socket_impl_init(&mobile->socket);

    // Initialize mobile library
    mobile->adapter = mobile_new(mobile);
    mobile_def_debug_log(mobile->adapter, impl_debug_log);
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
    mobile_config_set_dns(mobile->adapter, &dns1, MOBILE_DNS1);
    mobile_config_set_dns(mobile->adapter, &dns2, MOBILE_DNS2);
    mobile_config_set_p2p_port(mobile->adapter, p2p_port);
    mobile_config_set_relay(mobile->adapter, &relay);
    if (relay_token_update) {
        mobile_config_set_relay_token(mobile->adapter, relay_token);
    }
    mobile_config_save(mobile->adapter);

    // Initialize windows sockets
#ifdef _WIN32
    WSADATA wsaData;
    int wsa_err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_err != NO_ERROR) {
        fprintf(stderr, "WSAStartup failed with error: %d\n", wsa_err);
        goto error;
    }
#endif

    // Connect to the emulator
    SOCKET bgb_sock = socket_connect(host, port);
    if (bgb_sock == INVALID_SOCKET) {
        fprintf(stderr, "Could not connect (%s:%s): ", host, port);
        socket_perror(NULL);
        goto error;
    }
    if (setsockopt(bgb_sock, IPPROTO_TCP, TCP_NODELAY,
            (void *)&(int){1}, sizeof(int)) == SOCKET_ERROR) {
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
#elif defined(_WIN32)
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        fprintf(stderr, "SetConsoleCtrlHandler failed\n");
        goto error;
    }
#endif
    update_title(mobile);

    // Connect to the emulator
    struct bgb_state bgb_state;
    if (!bgb_init(&bgb_state, bgb_sock, MOBILE_SERIAL_IDLE_BYTE,
            bgb_loop_transfer, bgb_loop_timestamp, mobile)) {
        goto error;
    }

    // Wait for the timestamp to be initialized
    bgb_state.callback_timestamp = bgb_loop_timestamp_init;
    while (!mobile->bgb_clock_init) if (!bgb_loop(&bgb_state)) goto error;
    bgb_state.callback_timestamp = bgb_loop_timestamp;

    // Start main mobile thread
    mobile_start(mobile->adapter);

    while (!signal_int_trig) {
        if (!bgb_loop(&bgb_state)) break;
        if (!mobile_handle_loop(mobile)) break;

        // Wait for any of the sockets to do something
        // Time out after 100ms
        SOCKET sockets[1 + MOBILE_MAX_CONNECTIONS];
        unsigned socket_count = 0;
        sockets[socket_count++] = bgb_sock;
        for (unsigned i = 0; i < MOBILE_MAX_CONNECTIONS; i++) {
            SOCKET fd = mobile->socket.sockets[i];
            if (fd == INVALID_SOCKET) continue;
            sockets[socket_count++] = fd;
        }
        socket_wait(sockets, socket_count, 100);
    }
    signal_int_trig = true;

    // Wait for the mobile thread to finish
    mobile_stop(mobile->adapter);

    // Close all sockets
    socket_impl_stop(&mobile->socket);
    socket_close(bgb_sock);

#ifdef _WIN32
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
