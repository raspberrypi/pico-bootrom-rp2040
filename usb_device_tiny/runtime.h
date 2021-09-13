/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RUNTIME_H
#define _RUNTIME_H

#include "pico.h"

#include "hardware/structs/sio.h"
#include "hardware/structs/timer.h"

#ifdef USE_PICOBOOT
// allow usb_boot to not include all interfaces
#define USB_BOOT_WITH_SUBSET_OF_INTERFACES
// use a fixed number of interfaces to save code
#define USB_FIXED_INTERFACE_COUNT 2
#else
#define USB_FIXED_INTERFACE_COUNT 1
#endif

#ifndef USE_16BIT_ROM_FUNCS
#define __rom_function_ref(x) x
#define __rom_function_impl(t,x) t x
#define __rom_function_type(t) t
#define __rom_function_deref(t, x) x
#else
#define __rom_function_ref(x) ((ROM_FUNC ## x)*2)
#define __rom_function_impl(t, x) __used t impl ## x
#define __rom_function_type(t) uint8_t

#define __rom_function_deref_guts(x) ((uintptr_t)(*(uint16_t*)(_rom_functions + x)))
#define __rom_function_deref(t, x) ((t)__rom_function_deref_guts(x))

#define ROM_FUNC_usb_transfer_current_packet_only 1
#ifdef USE_PICOBOOT
#define ROM_FUNC_picoboot_cmd_packet 2
#endif
#define ROM_FUNC_msc_cmd_packet 3
#define ROM_FUNC_usb_stream_packet_packet_handler 4
#define ROM_FUNC_msc_on_sector_stream_chunk 5
#ifdef USE_PICOBOOT
#define ROM_FUNC_picoboot_on_stream_chunk 6
#endif

extern uint8_t _rom_functions[];
#endif

#define __rom_function_static_impl(t, x) /*static */__rom_function_impl(t, x)

extern void poor_mans_text_decompress(const uint8_t *src, uint32_t size, uint8_t *dest);

typedef unsigned int uint;

#define count_of(a) (sizeof(a)/sizeof((a)[0]))

extern void *__memcpy(void *dest, const void *src, uint n);
// Rom version
#define memcpy __memcpy
extern void memset0(void *dest, uint count);
void interrupt_enable(uint int_num, bool enable);
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);
extern bool watchdog_rebooting();

#ifdef USE_BOOTROM_GPIO
extern void gpio_setup();
// bit mask of pins for "disk activity"
extern uint32_t usb_activity_gpio_pin_mask;
#endif

#ifndef USB_BOOT_EXPANDED_RUNTIME
#define usb_trace(format, ...) ((void)0)
#define usb_debug(format, ...) ((void)0)
#define usb_warn(format, ...) ((void)0)
#define usb_panic(format, ...) __breakpoint()
#define uf2_debug(format, ...) ((void)0)
#define printf(format, ...) ((void)0)
#define puts(str) ((void)0)

extern uint32_t ctz32(uint32_t x);

#ifdef USE_BOOTROM_GPIO

#define PIN_DBG1 19

// note these two macros may only be used once per complilation unit
#define CU_REGISTER_DEBUG_PINS(p...) enum __unused DEBUG_PIN_TYPE { _none = 0, p }; static enum DEBUG_PIN_TYPE __selected_debug_pins;
#define CU_SELECT_DEBUG_PINS(x) static enum DEBUG_PIN_TYPE __selected_debug_pins = (x);

// Drive high every GPIO appearing in mask
static inline void gpio_set_mask(uint32_t mask) {
    *(volatile uint32_t *) (SIO_BASE + SIO_GPIO_OUT_SET_OFFSET) = mask;
}

// Drive low every GPIO appearing in mask
static inline void gpio_clr_mask(uint32_t mask) {
    *(volatile uint32_t *) (SIO_BASE + SIO_GPIO_OUT_CLR_OFFSET) = mask;
}

// Toggle every GPIO appearing in mask
static inline void gpio_xor_mask(uint32_t mask) {
    *(volatile uint32_t *) (SIO_BASE + SIO_GPIO_OUT_XOR_OFFSET) = mask;
}

#define DEBUG_PINS_ENABLED(p) (__selected_debug_pins == (p))
#define DEBUG_PINS_SET(p, v) if (DEBUG_PINS_ENABLED(p)) gpio_set_mask((unsigned)(v)<<PIN_DBG1)
#define DEBUG_PINS_CLR(p, v) if (DEBUG_PINS_ENABLED(p)) gpio_clr_mask((unsigned)(v)<<PIN_DBG1)
#define DEBUG_PINS_XOR(p, v) if (DEBUG_PINS_ENABLED(p)) gpio_xor_mask((unsigned)(v)<<PIN_DBG1)
#else
// note these two macros may only be used once per complilation unit
#define CU_REGISTER_DEBUG_PINS(p...)
#define CU_SELECT_DEBUG_PINS(x)

#define DEBUG_PINS_ENABLED(p) false
#define DEBUG_PINS_SET(p,v) ((void)0)
#define DEBUG_PINS_CLR(p,v) ((void)0)
#define DEBUG_PINS_XOR(p,v) ((void)0)
#endif

static inline uint32_t time_us_32() {
    return timer_hw->timerawl;
}

#else
#include "debug.h"
extern void uart_init(int i, int rate, int hwflow);
extern void panic(const char *fmt, ...);
extern int printf(const char *fmt, ...);
extern int puts(const char *str);
#define usb_panic(format,args...) panic(format, ## args)
#define usb_warn(format,args...) ({printf("WARNING: "); printf(format, ## args); })
#if false && !defined(NDEBUG)
#define usb_debug(format,args...) printf(format, ## args)
#else
#define usb_debug(format,...) ((void)0)
#endif
#if false && !defined(NDEBUG)
#define usb_trace(format,args...) printf(format, ## args)
#else
#define usb_trace(format,...) ((void)0)
#endif
#if true && !defined(NDEBUG)
#define uf2_debug(format,args...) printf(format, ## args)
#else
#define uf2_debug(format,...) ((void)0)
#endif
#define ctz32 __builtin_ctz
#endif
#endif
