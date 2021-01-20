/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _BOOTROM_CRC32_H
#define _BOOTROM_CRC32_H

#include "pico/types.h"

uint32_t crc32_small(const uint8_t *buf, unsigned int len, uint32_t seed);

#endif
