/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _USB_BOOT_DEVICE_H
#define _USB_BOOT_DEVICE_H

#include "usb_device.h"

#define VENDOR_ID   0x2e8au
#define PRODUCT_ID  0x0003u

void usb_boot_device_init(uint32_t _usb_disable_interface_mask);

void safe_reboot(uint32_t addr, uint32_t sp, uint32_t delay_ms);

// note these are inclusive to save - 1 checks... we always test the start and end of a range, so the range would have to be zero length which we don't use
static inline bool is_address_ram(uint32_t addr) {
    // todo allow access to parts of USB ram?
    return (addr >= SRAM_BASE && addr <= SRAM_END) ||
           (addr >= XIP_SRAM_BASE && addr <= XIP_SRAM_END);
}

static inline bool is_address_flash(uint32_t addr) {
    // todo maybe smaller?
    return (addr >= XIP_MAIN_BASE && addr <= SRAM_BASE);
}

static inline bool is_address_rom(uint32_t addr) {
    return addr < 8192;
}

// zero terminated
extern char serial_number_string[13];

#endif //_USB_BOOT_DEVICE_H
