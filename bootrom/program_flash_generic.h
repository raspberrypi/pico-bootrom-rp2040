/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PROGRAM_FLASH_GENERIC_H
#define _PROGRAM_FLASH_GENERIC_H

#include <stdint.h>
#include <stddef.h>

void connect_internal_flash();
void flash_init_spi();
void flash_put_get(const uint8_t *tx, uint8_t *rx, size_t count, size_t rx_skip);
void flash_do_cmd(uint8_t cmd, const uint8_t *tx, uint8_t *rx, size_t count);
void flash_exit_xip();
void flash_page_program(uint32_t addr, const uint8_t *data);
void flash_range_program(uint32_t addr, const uint8_t *data, size_t count);
void flash_sector_erase(uint32_t addr);
void flash_user_erase(uint32_t addr, uint8_t cmd);
void flash_range_erase(uint32_t addr, size_t count, uint32_t block_size, uint8_t block_cmd);
void flash_read_data(uint32_t addr, uint8_t *rx, size_t count);
int flash_size_log2();
void flash_flush_cache();
void flash_enter_cmd_xip();
void flash_abort();
int flash_was_aborted();

#endif // _PROGRAM_FLASH_GENERIC_H_
