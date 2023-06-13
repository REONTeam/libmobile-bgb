// SPDX-License-Identifier: GPL-3.0-or-later
#include "bgblink.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "socket.h"

// Attribute packed
#if defined(__GNUC__)
#define A_PACKED(...) __VA_ARGS__ __attribute__((packed))
#elif defined(_MSC_VER)
#define A_PACKED(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#else
#define A_PACKED(...) __VA_ARGS__
#endif

#define BGB_STATUS_RUNNING (1 << 0)
#define BGB_STATUS_PAUSED (1 << 1)
#define BGB_STATUS_SUPPORTRECONNECT (1 << 2)

enum bgb_cmd {
    BGB_CMD_VERSION = 1,
    BGB_CMD_JOYPAD = 101,
    BGB_CMD_SYNC1 = 104,
    BGB_CMD_SYNC2,
    BGB_CMD_SYNC3,
    BGB_CMD_STATUS = 108,
    BGB_CMD_WANTDISCONNECT
};

A_PACKED(struct bgb_packet {
    unsigned char cmd;
    unsigned char b2;
    unsigned char b3;
    unsigned char b4;
    uint32_t timestamp;
});

static const struct bgb_packet handshake = {
    .cmd = BGB_CMD_VERSION,
    .b2 = 1,
    .b3 = 4,
    .b4 = 0,
    .timestamp = 0,
};

static bool bgb_send(SOCKET socket, struct bgb_packet *buf)
{
    ssize_t num = send(socket, (char *)buf, sizeof(struct bgb_packet), 0);
    if (num == -1) {
        socket_perror("bgb_send");
        return false;
    }
    return num == sizeof(struct bgb_packet);
}

static bool bgb_recv(SOCKET socket, struct bgb_packet *buf)
{
    ssize_t num = recv(socket, (char *)buf, sizeof(struct bgb_packet), 0);
    if (num == -1) {
        socket_perror("bgb_recv");
        return false;
    }
    return num == sizeof(struct bgb_packet);
}

bool bgb_init(struct bgb_state *state, SOCKET socket, unsigned char init_byte, bgb_transfer_cb callback_transfer, bgb_timestamp_cb callback_timestamp, void *user)
{
    struct bgb_packet packet;

    state->user = user;
    state->socket = socket;
    state->callback_transfer = callback_transfer;
    state->callback_timestamp = callback_timestamp;
    state->byte = init_byte;
    state->timestamp_last = 0;
    state->timestamp_init = false;

    // Handshake
    memcpy(&packet, &handshake, sizeof(packet));
    if (!bgb_send(socket, &packet)) return false;
    if (!bgb_recv(socket, &packet)) return false;
    if (memcmp(&packet, &handshake, sizeof(packet)) != 0) {
        fprintf(stderr, "bgb_loop: Invalid handshake\n");
        return false;
    }

    // Send initial status
    packet.cmd = BGB_CMD_STATUS;
    packet.b2 = BGB_STATUS_RUNNING | BGB_STATUS_PAUSED |
        BGB_STATUS_SUPPORTRECONNECT;
    packet.b3 = 0;
    packet.b4 = 0;
    packet.timestamp = 0;
    if (!bgb_send(socket, &packet)) return false;

    // Expect a status packet back
    if (!bgb_recv(socket, &packet)) return false;
    if (packet.cmd != BGB_CMD_STATUS) {
        fprintf(stderr, "bgb_init: unexpected packet (1)\n");
        return false;
    }

    // Unpause the emulator
    packet.cmd = BGB_CMD_STATUS;
    packet.b2 = BGB_STATUS_RUNNING | BGB_STATUS_SUPPORTRECONNECT;
    packet.b3 = 0;
    packet.b4 = 0;
    if (!bgb_send(socket, &packet)) return false;

    return true;
}

bool bgb_loop(struct bgb_state *state)
{
    struct bgb_packet packet;
    unsigned char byte_cur;
    uint32_t timestamp_cur = state->timestamp_last;

    if (socket_hasdata(state->socket) <= 0) return true;
    if (!bgb_recv(state->socket, &packet)) return false;

    switch (packet.cmd) {
    case BGB_CMD_JOYPAD:
        // Not relevant
        break;

    case BGB_CMD_SYNC1:
        byte_cur = packet.b2;
        timestamp_cur = packet.timestamp;
        packet.cmd = BGB_CMD_SYNC2;
        packet.b2 = state->byte;
        packet.b3 = 0x80;
        packet.b4 = 0;
        packet.timestamp = 0;
        if (!bgb_send(state->socket, &packet)) return false;
        if (state->callback_transfer) {
            state->byte = state->callback_transfer(state->user, byte_cur);
        }
        break;

    case BGB_CMD_SYNC2:
        // Can be sent if the game has queued up a byte to send as slave...
        // Not necessary to reply.
        break;

    case BGB_CMD_SYNC3:
        timestamp_cur = packet.timestamp;
        if (packet.b2 != 0) break;
        if (!bgb_send(state->socket, &packet)) return false;
        break;

    case BGB_CMD_STATUS:
        // Ignore, we've already sent a status packet
        break;

    default:
        fprintf(stderr, "bgb_loop: Unknown command: %d (%02X %02X %02X) @ %d\n",
            packet.cmd, packet.b2, packet.b3, packet.b4, packet.timestamp);
        return false;
    }

    if (state->callback_timestamp) {
        if (!state->timestamp_init) {
            state->callback_timestamp(state->user, timestamp_cur);
            state->timestamp_last = timestamp_cur;
            state->timestamp_init = true;
        }

        // Attempt to detect the clock going back in time
        // This is probably a BGB bug, caused by enabling some options,
        //   such as the "break on ld d,d" option.
        uint32_t diff = (state->timestamp_last - timestamp_cur) & 0x7FFFFFFF;
        if (diff != 0 && diff <= 0x100) {
            fprintf(stderr, "[BGB] Emulator went back in time? "
                "old: 0x%08X; new: 0x%08X\n",
                state->timestamp_last, timestamp_cur);
            timestamp_cur = state->timestamp_last;
        }

        if (state->timestamp_last != timestamp_cur) {
            state->callback_timestamp(state->user, timestamp_cur);
        }
        state->timestamp_last = timestamp_cur;
    }

    return true;
}

// Bad attmept at implementing link support with the VBA-M emulator.
// This emulator doesn't implement link support for GBA's normal mode,
//   and "game boy" link mode for GB/C (implemented here) randomly drops bytes.
// Last version checked: v2.1.4
#if 0
static bool vba_send(int socket, const unsigned char *data, unsigned size)
{
    ssize_t num = send(socket, (char *)data, size, 0);
    if (num == -1) {
        socket_perror("vba_send");
        return false;
    }
    printf("> ");
    for (int i = 0; i < size; i++) printf("%02X", data[i]);
    printf("\n");
    return true;
}

static bool vba_recv(int socket, unsigned char *data, unsigned size)
{
    ssize_t num = recv(socket, (char *)data, size, 0);
    if (num == -1) {
        socket_perror("vba_recv");
        return false;
    }
    printf("< ");
    for (int i = 0; i < size; i++) printf("%02X", data[i]);
    printf("\n");
    return true;
}

// VBA default port: 5738
void vba_loop(int socket, unsigned char (*callback_transfer)(void *, unsigned char), void (*callback_timestamp)(void *, uint32_t), void *user)
{
    (void)callback_timestamp;
    unsigned char data[8];

    if (!vba_recv(socket, data, 8)) return;
    if (!vba_recv(socket, data, 5)) return;
    data[0] = 0xD2;
    if (!vba_send(socket, data, 1)) return;

    unsigned char transfer_last = 0xD2;
    unsigned char transfer_cur;

    for (;;) {
        if (!vba_recv(socket, data, 1)) return;
        transfer_cur = data[0];
        data[0] = transfer_last;
        if (!vba_send(socket, data, 1)) return;
        transfer_last = callback_transfer(user, transfer_cur);
    }
}
#endif
