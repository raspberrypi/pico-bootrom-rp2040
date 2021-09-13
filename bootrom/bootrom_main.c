/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "program_flash_generic.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/structs/xosc.h"
#include "hardware/sync.h"
#include "hardware/resets.h"
#include "usb_boot_device.h"
#include "resets.h"

#include "async_task.h"
#include "bootrom_crc32.h"
#include "runtime.h"
#include "hardware/structs/usb.h"

// From SDF + STA, plus 20% margin each side
// CLK_SYS FREQ ON STARTUP (in MHz)
// +-----------------------
// | min    |  1.8        |
// | typ    |  6.5        |
// | max    |  11.3       |
// +----------------------+
#define ROSC_MHZ_MAX 12

// Each attempt takes around 4 ms total with a 6.5 MHz boot clock
#define FLASH_MAX_ATTEMPTS 128

#define BOOT2_SIZE_BYTES 256
#define BOOT2_FLASH_OFFS 0
#define BOOT2_MAGIC 0x12345678
#define BOOT2_BASE (SRAM_END - BOOT2_SIZE_BYTES)


static uint8_t *const boot2_load = (uint8_t *const) BOOT2_BASE;
static ssi_hw_t *const ssi = (ssi_hw_t *) XIP_SSI_BASE;

extern void debug_trampoline();

// 3 cycles per count
static inline void delay(uint32_t count) {
    asm volatile (
    "1: \n\t"
    "sub %0, %0, #1 \n\t"
    "bne 1b"
    : "+r" (count)
    );
}

static void _flash_boot() {
    connect_internal_flash();
    flash_exit_xip();

    // Repeatedly poll flash read with all CPOL CPHA combinations until we
    // get a valid 2nd stage bootloader (checksum pass)
    int attempt;
    for (attempt = 0; attempt < FLASH_MAX_ATTEMPTS; ++attempt) {
        unsigned int cpol_cpha = attempt & 0x3u;
        ssi->ssienr = 0;
        ssi->ctrlr0 = (ssi->ctrlr0
                       & ~(SSI_CTRLR0_SCPH_BITS | SSI_CTRLR0_SCPOL_BITS))
                      | (cpol_cpha << SSI_CTRLR0_SCPH_LSB);
        ssi->ssienr = 1;

        flash_read_data(BOOT2_FLASH_OFFS, boot2_load, BOOT2_SIZE_BYTES);
        uint32_t sum = crc32_small(boot2_load, BOOT2_SIZE_BYTES - 4, 0xffffffff);
        if (sum == *(uint32_t *) (boot2_load + BOOT2_SIZE_BYTES - 4))
            break;
    }

    if (attempt == FLASH_MAX_ATTEMPTS)
        return;

    // Take this opportunity to flush the flash cache, as the debugger may have
    // written fresh code in behind it.
    flash_flush_cache();

    // Enter boot2 (thumb bit set). Exit pointer is passed in lr -- we pass
    // null, boot2 provides default for this case.  
    // Addition performed inside asm because GCC *really* wants to store another constant
    uint32_t boot2_entry = (uintptr_t) boot2_load;
    const uint32_t boot2_exit = 0;
    asm volatile (
    "add %0, #1\n"
    "mov lr, %1\n"
    "bx %0\n"
    : "+r" (boot2_entry) : "l" (boot2_exit) :
    );
    __builtin_unreachable();
}

// USB bootloader requires clk_sys and clk_usb at 48 MHz. For this to work,
// xosc must be running at 12 MHz. It is possible that:
//
// - No crystal is present (and XI may not be properly grounded)
// - xosc output is much greater than 12 MHz
//
// In this case we *must* leave clk_sys in a safe state, and ideally, never
// return from this function. This is because boards which are not designed to
// use USB will still enter the USB bootcode when booted with a blank flash.

static void _usb_clock_setup() {
    // First make absolutely sure clk_ref is running: needed for resuscitate,
    // and to run clk_sys while configuring sys PLL. Assume that rosc is not
    // configured to run faster than clk_sys max (as this is officially out of
    // spec)
    // If user previously configured clk_ref to a different source (e.g.
    // GPINx), then halted that source, the glitchless mux can't switch away
    // from the dead source-- nothing we can do about this here.
    rosc_hw->ctrl = ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB;
    hw_clear_bits(&clocks_hw->clk[clk_ref].ctrl, CLOCKS_CLK_REF_CTRL_SRC_BITS);

    // Resuscitate logic will switch clk_sys to clk_ref if it is inadvertently stopped
    clocks_hw->resus.ctrl =
            CLOCKS_CLK_SYS_RESUS_CTRL_ENABLE_BITS |
            (CLOCKS_CLK_SYS_RESUS_CTRL_TIMEOUT_RESET
                    << CLOCKS_CLK_SYS_RESUS_CTRL_TIMEOUT_LSB);

    // Resetting PLL regs or changing XOSC range can glitch output, so switch
    // clk_sys away before touching. Not worried about clk_usb as USB is held
    // in reset.
    hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (!(clocks_hw->clk[clk_sys].selected & 1u));
    // rosc can not (while following spec) run faster than clk_sys max, so
    // it's safe now to clear dividers in clkslices.
    clocks_hw->clk[clk_sys].div = 0x100; // int 1 frac 0
    clocks_hw->clk[clk_usb].div = 0x100;

    // Try to get the crystal running. If no crystal is present, XI should be
    // grounded, so STABLE counter will never complete. Poor designs might
    // leave XI floating, in which case we may eventually drop through... in
    // this case we rely on PLL not locking, and/or resuscitate counter.
    //
    // Don't touch range setting: user would only have changed if crystal
    // needs it, and running crystal out of range can produce glitchy output.
    // Note writing a "bad" value (non-aax) to RANGE has no effect.
    xosc_hw->ctrl = XOSC_CTRL_ENABLE_VALUE_ENABLE << XOSC_CTRL_ENABLE_LSB;
    while (!(xosc_hw->status & XOSC_STATUS_STABLE_BITS));

    // Sys PLL setup:
    // - VCO freq 1200 MHz, so feedback divisor of 100. Range is 400 MHz to 1.6 GHz
    // - Postdiv1 of 5, down to 240 MHz (appnote recommends postdiv1 >= postdiv2)
    // - Postdiv2 of 5, down to 48 MHz
    //
    // Total postdiv of 25 means that too-fast xtal will push VCO out of
    // lockable range *before* clk_sys goes out of closure (factor of 1.88)
    reset_unreset_block_wait_noinline(RESETS_RESET_PLL_SYS_BITS);
    pll_sys_hw->cs = 1u << PLL_CS_REFDIV_LSB;
    pll_sys_hw->fbdiv_int = 100;
    pll_sys_hw->prim =
            (5u << PLL_PRIM_POSTDIV1_LSB) |
            (5u << PLL_PRIM_POSTDIV2_LSB);

    // Power up VCO, wait for lock
    hw_clear_bits(&pll_sys_hw->pwr, PLL_PWR_PD_BITS | PLL_PWR_VCOPD_BITS);
    while (!(pll_sys_hw->cs & PLL_CS_LOCK_BITS));

    // Power up post-dividers, which ungates PLL final output
    hw_clear_bits(&pll_sys_hw->pwr, PLL_PWR_POSTDIVPD_BITS);

    // Glitchy switch of clk_usb, clk_sys aux to sys PLL output.
    clocks_hw->clk[clk_sys].ctrl = 0;
    clocks_hw->clk[clk_usb].ctrl =
            CLOCKS_CLK_USB_CTRL_ENABLE_BITS |
            (CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS
                    << CLOCKS_CLK_USB_CTRL_AUXSRC_LSB);

    // Glitchless switch of clk_sys to aux source (sys PLL)
    hw_set_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (!(clocks_hw->clk[clk_sys].selected & 0x2u));
}

void __noinline __attribute__((noreturn)) async_task_worker_thunk();

static __noinline __attribute__((noreturn)) void _usb_boot(uint32_t _usb_activity_gpio_pin_mask,
                                                                  uint32_t disable_interface_mask) {
    reset_block_noinline(RESETS_RESET_USBCTRL_BITS);
    if (!running_on_fpga())
        _usb_clock_setup();
    unreset_block_wait_noinline(RESETS_RESET_USBCTRL_BITS);

    // Ensure timer and watchdog are running at approximately correct speed
    // (can't switch clk_ref to xosc at this time, as we might lose ability to resus)
    watchdog_hw->tick = 12u << WATCHDOG_TICK_CYCLES_LSB;
    hw_set_bits(&watchdog_hw->tick, WATCHDOG_TICK_ENABLE_BITS);

    // turn off XIP cache since we want to use it as RAM in case the USER wants to use it for a RAM only binary
    hw_clear_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_EN_BITS);
    // Don't clear out RAM - leave it to binary download to clear anything it needs cleared; anything BSS will be done by crt0.S on reset anyway

    // this is where the BSS is so clear it
    memset0(usb_dpram, USB_DPRAM_SIZE);

    // now we can finally initialize these
#ifdef USE_BOOTROM_GPIO
    usb_activity_gpio_pin_mask = _usb_activity_gpio_pin_mask;
#endif

    usb_boot_device_init(disable_interface_mask);

    // worker to run tasks on this thread (never returns); Note: USB code is IRQ driven
    // this thunk switches stack into USB DPRAM then calls async_task_worker
    async_task_worker_thunk();
}

static void __attribute__((noreturn)) _usb_boot_reboot_wrapper() {
    _usb_boot(watchdog_hw->scratch[0], watchdog_hw->scratch[1]);
}

void __attribute__((noreturn)) reset_usb_boot(uint32_t _usb_activity_gpio_pin_mask, uint32_t _disable_interface_mask) {
    watchdog_hw->scratch[0] = _usb_activity_gpio_pin_mask;
    watchdog_hw->scratch[1] = _disable_interface_mask;
    watchdog_reboot((uintptr_t) _usb_boot_reboot_wrapper, SRAM_END, 10);
    while (true) __wfi();
}

int main() {
    const uint32_t rst_mask =
            RESETS_RESET_IO_QSPI_BITS |
            RESETS_RESET_PADS_QSPI_BITS |
            RESETS_RESET_TIMER_BITS;
    reset_unreset_block_wait_noinline(rst_mask);

    // Workaround for behaviour of TXB0108 bidirectional level shifters on
    // FPGA platform (JIRA PRJMU-726), not used on ASIC
    if (running_on_fpga()) {
        *(io_rw_32 *) (IO_QSPI_BASE + 0xc) = 5; // GPIO_FUNC_PROC
        sio_hw->gpio_hi_out = 1u << 1;         // Level high on CS pin
        sio_hw->gpio_hi_oe = 1u << 1;          // Output enable
        sio_hw->gpio_hi_oe = 0;                // Output disable
    }

    // Check CSn strap: delay for pullups to charge trace, then take a majority vote.
    delay(100 * ROSC_MHZ_MAX / 3);
    uint32_t sum = 0;
    for (int i = 0; i < 9; ++i) {
        delay(1 * ROSC_MHZ_MAX / 3);
        sum += (sio_hw->gpio_hi_in >> 1) & 1u;
    }

    if (sum >= 5)
        _flash_boot();

    // note this never returns (and is marked as such)
    _usb_boot(0, 0);
}
