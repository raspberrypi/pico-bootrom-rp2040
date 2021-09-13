/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "hardware/regs/m0plus.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/watchdog.h"
#include "hardware/resets.h"

#include "runtime.h"

#ifdef USE_BOOTROM_GPIO

#include "hardware/regs/pads_bank0.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/sio.h"

#endif

#ifdef USE_16BIT_ROM_FUNCS
asm (
".global _rom_functions\n"
"_rom_functions:\n"
".hword _dead + 1\n" // should not be called
".hword impl_usb_transfer_current_packet_only + 1\n"
#ifdef USE_PICOBOOT
".hword impl_picoboot_cmd_packet + 1\n"
#else
".hword _dead + 1\n" // should not be called
#endif
".hword impl_msc_cmd_packet + 1\n"
".hword impl_usb_stream_packet_packet_handler + 1\n"
".hword impl_msc_on_sector_stream_chunk + 1\n"
#ifdef USE_PICOBOOT
".hword impl_picoboot_on_stream_chunk + 1\n"
#endif
);
#endif

void memset0(void *dest, uint n) {
    extern void __memset(void *dest, int c, uint n);
    __memset(dest, 0, n);
}

volatile bool rebooting;

// note this is always false in USB_BOOT_EXPANDED_RUNTIME but that is ok
bool watchdog_rebooting() {
    return rebooting;
}

#ifndef USB_BOOT_EXPANDED_RUNTIME

void interrupt_enable(uint irq, bool enable) {
    assert(irq < N_IRQS);
    if (enable) {
        // Clear pending before enable
        // (if IRQ is actually asserted, it will immediately re-pend)
        *(volatile uint32_t *) (PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET) = 1u << irq;
        *(volatile uint32_t *) (PPB_BASE + M0PLUS_NVIC_ISER_OFFSET) = 1u << irq;
    } else {
        *(volatile uint32_t *) (PPB_BASE + M0PLUS_NVIC_ICER_OFFSET) = 1u << irq;
    }
}

void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    check_hw_layout(watchdog_hw_t, scratch[7], WATCHDOG_SCRATCH7_OFFSET);
    // Set power regs such that everything is reset by the watchdog
    // except ROSC, XOSC, and RUN_DEBOUNCED (resets the glitchless mux in clkslice).
    // One of which will be the watchdog tick
    *((volatile uint32_t *) (PSM_BASE + PSM_WDSEL_OFFSET)) =
            PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS);
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS | WATCHDOG_CTRL_PAUSE_DBG0_BITS | WATCHDOG_CTRL_PAUSE_DBG1_BITS |
                                      WATCHDOG_CTRL_PAUSE_JTAG_BITS); // want to reboot even under debugger
    if (pc) {
        pc |= 1u; // thumb mode
        watchdog_hw->scratch[4] = 0xb007c0d3;
        watchdog_hw->scratch[5] = pc ^ -watchdog_hw->scratch[4];
        watchdog_hw->scratch[6] = sp;
        watchdog_hw->scratch[7] = pc;
    } else {
        watchdog_hw->scratch[4] = 0;
    }
    watchdog_hw->load = delay_ms * 1000;
    // Switch clk_ref to crystal, then ensure watchdog is ticking with a
    // divisor of 12. For USB we require a 12 MHz crystal.
    if (!running_on_fpga()) {
        // Note clock mux not present on FPGA (clk_ref hardwired to 12 MHz)
        hw_write_masked(&clocks_hw->clk[clk_ref].ctrl, CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                       CLOCKS_CLK_REF_CTRL_SRC_BITS);
        while (!(clocks_hw->clk[clk_ref].selected & (1u << CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC)));
    }
    watchdog_hw->tick = 12u << WATCHDOG_TICK_CYCLES_LSB;
    hw_set_bits(&watchdog_hw->tick, WATCHDOG_TICK_ENABLE_BITS);
    hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    rebooting = true;
}

#endif

#ifdef USE_BOOTROM_GPIO

#ifdef USB_BOOT_EXPANDED_RUNTIME
#include "gpio.h"
#else

static inline io_rw_32 *GPIO_CTRL_REG(uint x) {
    uint32_t offset = 0;

    // removed since we know it is in range
//    if (x >=  0 && x < 32)
    offset = IO_BANK0_BASE + (x * 8) + 4;

    return (io_rw_32 *) offset;
}

static inline io_rw_32 *PAD_CTRL_REG(uint x) {
    uint32_t offset = 0;

    // removed since we know it is in range
//    if (x >=  0 && x < 32)
    offset = PADS_BANK0_BASE + PADS_BANK0_GPIO0_OFFSET + (x * 4);

    return (io_rw_32 *) offset;
}

void gpio_funcsel(uint i, int fn) {
    io_rw_32 *pad_ctl = PAD_CTRL_REG(i);
    // we are only enabling output, so just clear the OD
    hw_clear_bits(pad_ctl, PADS_BANK0_GPIO0_OD_BITS);
    // Set the funcsel
    *GPIO_CTRL_REG(i) = (fn << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB);
}

#endif

uint32_t usb_activity_gpio_pin_mask;

void gpio_setup() {
    if (usb_activity_gpio_pin_mask) {
        unreset_block(RESETS_RESET_IO_BANK0_BITS);
        sio_hw->gpio_set = usb_activity_gpio_pin_mask;
        sio_hw->gpio_oe_set = usb_activity_gpio_pin_mask;
        // need pin number rather than mask
        gpio_funcsel(ctz32(usb_activity_gpio_pin_mask), 5);
    }
#ifndef NDEBUG
    // Set to RIO for debug
    for (int i = 19; i < 23; i++) {
        gpio_init(i);
        gpio_dir_out_mask(1 << i);
    }
#endif
}

#endif
