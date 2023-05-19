// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "socket.h"

typedef unsigned char (*bgb_transfer_cb)(void *, unsigned char);
typedef void (*bgb_timestamp_cb)(void *, uint32_t);

struct bgb_state {
    // public
    void *user;
    SOCKET socket;
    unsigned char byte;
    bgb_transfer_cb callback_transfer;
    bgb_timestamp_cb callback_timestamp;

    // private
    uint32_t timestamp_last;
    bool timestamp_init;
};

void socket_perror(const char *func);
bool bgb_init(struct bgb_state *state, SOCKET socket, unsigned char init_byte, bgb_transfer_cb callback_transfer, bgb_timestamp_cb callback_timestamp, void *user);
bool bgb_loop(struct bgb_state *state);
