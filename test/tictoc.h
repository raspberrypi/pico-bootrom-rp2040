/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

static inline void tictoc_init() {
    *(volatile unsigned int *)0xe000e010=5; // enable SYSTICK at core clock
}

static inline unsigned int cyc() {
    return (~*(volatile unsigned int *)0xe000e018)<<8;
}


