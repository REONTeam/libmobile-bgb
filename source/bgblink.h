// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef unsigned char (*bgb_transfer_cb)(void *, unsigned char);
typedef void (*bgb_timestamp_cb)(void *, uint32_t);

struct bgb_state {
    // public
    void *user;
    int socket;
    bgb_transfer_cb callback_transfer;
    bgb_timestamp_cb callback_timestamp;

    // private
    unsigned char transfer_last;
    uint32_t timestamp_last;
    bool set_status;
};

void socket_perror(const char *func);
bool bgb_init(struct bgb_state *state, int socket, bgb_transfer_cb callback_transfer, bgb_timestamp_cb callback_timestamp, void *user);
bool bgb_loop(struct bgb_state *state);
