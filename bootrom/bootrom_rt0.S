/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */


// ----------------------------------------------------------------------------
// Bootrom Runtime 0
// ----------------------------------------------------------------------------
// This is not a full crt0 -- in particular, no .bss or .data initialisation
// (use of .data/.bss is disallowed via linker script assertions).
// The bootrom is not permitted to use statically-allocated memory, as parts of
// it are called into by user code.
// The purpose of this file is:
// - Provide initial entry point for both cores
// - Provide holding pen and launch code for core 1
// - Provide direct-boot entry for core 0, mainly intended for running
//   ARM code during ATE
// - Pass core 0 control over to the main flash boot sequence

#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/sio.h"
#include "hardware/regs/watchdog.h"
#include "hardware/regs/syscfg.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/vreg_and_chip_reset.h"
#include "hardware/regs/m0plus.h"
#include "git_info.h"

.cpu cortex-m0
.thumb
.section .vectors
.balign 2

.global __vectors
__vectors:
.word _stacktop // MSP
.word _start    // Reset
.word _nmi      // NMI
.word _dead     // HardFault

.global _magic
_magic:
# magic
.byte 'M', 'u'
# compatibility version (change if function table is incompatible, or functions are backwards incompatible)
.byte 1
# ROM version
.byte 3

.global _well_known
_well_known:
.hword function_table
.hword data_table
.hword table_lookup + 1
.hword 0 // pad

.global table_lookup
.type table_lookup,%function
.thumb_func
table_lookup:
    mov r3, #0
    ldrh r2, [r0]
    cmp r2, r3
    beq 1f
    ldrh r3, [r0, #2]
    add r0, #4
    cmp r1, r2
    bne table_lookup
1:
    mov r0, r3
    bx lr

.global _dead
.type _dead,%function
.thumb_func
_dead: // in place of irq4 vector
    wfi
    b _dead

// this is all a bit cheeky, but the existing code assumed that the table above could go there because nothing ever called
// some code in irq0 -4 slots

// If you reach this, something has gone wrong. Most likely, the debugger
// has done a core-only reset while the NMI mask was set, and a relevant
// IRQ was asserted. This is probably not intentional, and is confusing
// to debug, so we should just clear the NMI masks.
.align 2
.global _nmi
.type _nmi,%function
.thumb_func
_nmi:
    // we do not want to use any stack as we're called from the core 0 boot path.
    // we use r12 as this is saved/restored by the processor in an actual NMI
    mov r12, lr
    // We can take an NMI straight out of reset, so we should first ensure
    // that SYSCFG is being clocked, else we end up straight back in NMI
    bl enable_clocks
    // Then just clear the NMI mask (for both cores)
    ldr r0, =(SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK_OFFSET)
    mov r1, #0
    str r1, [r0]
    str r1, [r0, #4]
    mov pc, r12

// On a cold boot, the clocks will already be enabled, because the power-on state
// machine will have reset the clock controls. However we can have trouble on a warm
// boot, that is to say:
// - The debugger has just reset the processors and started them running
// - The watchdog has fired, with WDSEL selecting a restart point after
//   clocks_bank_default.
// Assume that enough clocks are already enabled to run this code!
// Note it is NOT recommended to disable things like ROM clock if WDSEL is
// later than CLOCKS_BANK_DEFAULT.
.global enable_clocks
.type enable_clocks,%function
.thumb_func
enable_clocks:
    ldr r0, =(CLOCKS_BASE + CLOCKS_WAKE_EN0_OFFSET)
    // Set entire clock enable mask. Relying on HCGs to avoid huge current transient
    mov r1, #0
    mvn r1, r1
    str r1, [r0]
    str r1, [r0, #4]
    // we steal the return for its own function
.global _noop
.type _noop,%function
.thumb_func
_noop:
    bx lr

.align 2
.global software_git_revision
software_git_revision:
.word GIT_REV

.global __irq5_vector
__irq5_vector:
.word isr_irq5

copyright:
.string "(C) 2020 Raspberry Pi Trading Ltd"


function_table:
# function table
#ifdef USE_POPCOUNT32
.byte 'P, '3'
.hword popcount32 + 1
#endif
#ifdef USE_REVERSE32
.byte 'R', '3'
.hword reverse32 + 1
#endif
#ifdef USE_CLZ32
.byte 'L', '3'
.hword clz32 + 1
#endif
#ifdef USE_CTZ32
.byte 'T', '3'
.hword ctz32 + 1
#endif
.byte 'M', 'S'
.hword __memset + 1
.byte 'S', '4'
.hword __memset_4 + 1
.byte 'M', 'C'
.hword __memcpy + 1
.byte 'C', '4'
.hword __memcpy_44 + 1
.byte 'U', 'B'
.hword reset_usb_boot + 1
.byte 'D', 'T'
.hword debug_trampoline + 1
.byte 'D', 'E'
.hword debug_trampoline_end + 1
.byte 'W', 'V'
.hword wait_for_vector + 1
.byte 'I', 'F'
.hword connect_internal_flash + 1
.byte 'E', 'X'
.hword flash_exit_xip + 1
.byte 'R', 'E'
.hword flash_range_erase + 1
.byte 'R', 'P'
.hword flash_range_program + 1
.byte 'F', 'C'
.hword flash_flush_cache + 1
.byte 'C', 'X'
.hword flash_enter_cmd_xip + 1
# end of function table marker
.hword 0

.global data_table
data_table:
    .byte 'G', 'R'
    .hword software_git_revision
    .byte 'C', 'R'
    .hword copyright
    .byte 'S', 'F'
    .hword soft_float_table
    .byte 'S', 'D'
    .hword soft_double_table
    .byte 'F', 'Z'
    .hword soft_float_table_size
    // expose library start and end to facilitate users copying into RAM
    .byte 'F, 'S'
    .hword mufp_lib_start
    .byte 'F, 'E'
    .hword mufp_lib_end
    // expose library start and end to facilitate users copying into RAM
    .byte 'D, 'S'
    .hword mufp_lib_double_start
    .byte 'D, 'E'
    .hword mufp_lib_double_end
    .hword 0

// ----------------------------------------------------------------------------
// Entry point for both cores
// ----------------------------------------------------------------------------

.global _start
.type _start,%function
.thumb_func
_start:

// Check if this is core 0, and go to holding pen if not
check_core:
    // NOTE: We DO NOT use any stack prior to possible watchdog entry (this includes NMI vector handler)
    ldr r0, =SIO_BASE
    ldr r1, [r0, #SIO_CPUID_OFFSET]
    cmp r1, #0
    bne wait_for_vector

// Make sure all the control registers we are about to access are being clocked.
// On a cold boot everything will be set up by the power-on state machine,
// but the clock setup may be dirty on a warm boot.

// note that the NMI handler does exactly what we want (enable_clocks) and also disables NMI
    bl _nmi

// If the rescue flag is set in PoR block, we should halt immediately.
// (presumably some lethal code is in flash which would stop the debugger from
// communicating with the processors).
check_rescue:
    ldr r1, =(VREG_AND_CHIP_RESET_BASE + VREG_AND_CHIP_RESET_CHIP_RESET_OFFSET)
    ldr r2, [r1]
#if VREG_AND_CHIP_RESET_CHIP_RESET_PSM_RESTART_FLAG_BITS != 0x01000000
#error
#endif
    lsr r3, r2, #25
    bcc 1f
// Acknowledge and halt (note we write all bits, but VREG_AND_CHIP_RESET_CHIP_RESET_PSM_RESTART_FLAG_BITS
// is the only WC bit
    str r2, [r1]
    b _dead
1:

disable_adc_ie:
#if RESETS_RESET_PADS_BANK0_BITS != (0x80 << 1)
#error
#endif
    mov r0, #0x80
    lsl r0, #1
    bl unreset_block_wait_noinline

    ldr r1, =PADS_BANK0_BASE + REG_ALIAS_CLR_BITS
    mov r2, #PADS_BANK0_GPIO0_IE_BITS
    str r2, [r1, #PADS_BANK0_GPIO26_OFFSET]
    str r2, [r1, #PADS_BANK0_GPIO27_OFFSET]
    str r2, [r1, #PADS_BANK0_GPIO28_OFFSET]
    str r2, [r1, #PADS_BANK0_GPIO29_OFFSET]

// Check watchdog scratch for direct-boot magic numbers
// This is useful in factory test for running ARM code without accessing DAP.
// Probably also useful for imaginative software engineers
// - Scratch 4:  0xb007c0d3
// - Scratch 5:  Entry point ^ -0xb007c0d3
// - Scratch 6:  Stack pointer
// - Scratch 7:  Entry point
check_wdog:
    ldr r7, =(WATCHDOG_BASE + WATCHDOG_SCRATCH4_OFFSET)
    ldr r6, =(0xb007c0d3)
    ldmia r7!, {r0, r1, r2, r3}
    cmp r0, r6
    bne 1f
    eor r1, r3
    add r0, r1
    bne 1f
    // Clear magic number for next time (note -16 because of r7! above)
    sub r7, #16
    str r0, [r7]
    // Magic numbers look good, so jump straight into the code that has been prepared for us
    msr msp, r2
    // Note if this return, we continue with regular boot below
    blx r3
    // Proceed to main flash boot sequence
1:
    // main does not return
    bl main
    // b _dead

// ----------------------------------------------------------------------------
// Hold/launch code for Core 1
// ----------------------------------------------------------------------------
// Core 0 will bring core 1 up once it has gone through the sequence of setting
// up flash etc.
//

send_and_then_again:
    // in case of multiple core 1 resets, we can keep pushing and fill the FIFO
    // we should wait for an event if the FIFO is full to avoid busy wait
    wfe
// takes r0 = word to send, r4 = SIOB_BASE, r5 link register
send_and_then:
    ldr r1, [r4, #SIO_FIFO_ST_OFFSET]
    lsr r1, #SIO_FIFO_ST_RDY_LSB + 1
    bcc send_and_then_again
    str r0, [r4, #SIO_FIFO_WR_OFFSET]
    sev
    add r6, r5, #1
    bx  r6

#define M0_BASE (PPB_BASE + M0PLUS_CPUID_OFFSET)

wait_for_vector:
    ldr r4, =SIO_BASE
    ldr r7, =M0_BASE
    // Enable SCR.SLEEPDEEP before WFE -- this allows NVIC to be fully gated during sleep
    mov r1, #M0PLUS_SCR_SLEEPDEEP_BITS
    str r1, [r7, #(M0PLUS_SCR_OFFSET - M0PLUS_CPUID_OFFSET)]
    // note core_0_handshake_loop is the intended next instruction, but the read is harmless
    // as we're about to drain, so don't waste an instruction branching
1:
    ldr r1, [r4, #SIO_FIFO_RD_OFFSET]
core_0_handshake_loop:
    // drain the FIFO before sending 0
    ldr r1, [r4, #SIO_FIFO_ST_OFFSET]
    lsr r1, #SIO_FIFO_ST_VLD_LSB + 1
    bcs 1b

    // ...and_then = receive_and_check_zero (which jmps to core_0_handshake_loop on 0)
    adr r5, receive_and_check_zero
    // send 0
    mov r0, #0
    bl send_and_then
    // check for cmd 1
    cmp r0, #1
    bne core_0_handshake_loop
    // ack and receive VTOR
    bl send_and_then
    str r0, [r7, #(M0PLUS_VTOR_OFFSET - M0PLUS_CPUID_OFFSET)]
    // ack and receive SP
    bl send_and_then
    // initialize
    msr msp, r0
    bl send_and_then
    adr r5, core1_launch
    // receive IP (0 sends us back into handshake loop)
    bl send_and_then
//    nop // .. for alignment
core1_launch:
    // Disable SLEEPDEEP before exiting, as it affects wake latency
    mov r1, #0
    str r1, [r7, #(M0PLUS_SCR_OFFSET - M0PLUS_CPUID_OFFSET)]
    blx r0

// Low power hang on return. Reset the core if you want to provide another entry point
// (There is no need to return though)
//
// alternatively you could return directly to wait_for_vector (available in the function table)
// if you know core 1 is still in a good state
    b _dead

.align 2
// takes r4 = SIOB_BASE
// returns r0 = word received
receive_and_check_zero:
    wfe
    ldr r0, [r4, #SIO_FIFO_ST_OFFSET]
    lsr r0, #SIO_FIFO_ST_VLD_LSB + 1
    bcc receive_and_check_zero

    ldr r0, [r4, #SIO_FIFO_RD_OFFSET]
    cmp r0, #0
    // if we received 0, we reset back to main loop
    beq core_0_handshake_loop
    bx  lr

.global reset_block_noinline
.type reset_block_noinline,%function
.thumb_func
reset_block_noinline:
    ldr r1, =RESETS_BASE + REG_ALIAS_SET_BITS
    str r0, [r1]
    bx lr

.global reset_unreset_block_wait_noinline
.type reset_unreset_block_wait_noinline,%function
.thumb_func
reset_unreset_block_wait_noinline:
    ldr r1, =RESETS_BASE + REG_ALIAS_SET_BITS
    str r0, [r1]
    // fall thru

.global unreset_block_wait_noinline
.type unreset_block_wait_noinline,%function
.thumb_func
unreset_block_wait_noinline:
    ldr r1, =RESETS_BASE
    ldr r2, =RESETS_BASE + REG_ALIAS_CLR_BITS
    str r0, [r2]
1:
    ldr r2, [r1, #RESETS_RESET_DONE_OFFSET]
    bic r0, r2
    bne 1b
    bx lr

// ----------------------------------------------------------------------------
// Simple debugger trampoline for break-on-return
// ----------------------------------------------------------------------------
// This is handy for the debugger calling ROM routines without setting hardware
// breakpoints, mainly useful for flash programming.
// Set function address in r7, pass args through r0...r3 as per ABI,
// then jump to this trampoline.

.global debug_trampoline
.type debug_trampoline,%function
.thumb_func
debug_trampoline:
    // Ensure the LSB is set (Thumb Mode) to avoid hardfault
    mov r6, #1
    orr r7, r7, r6
    blx r7
debug_trampoline_end:
    bkpt #0
    b debug_trampoline

    .byte 0x11, 0x38, 0xc0, 0x7a, 0x00, 0xbd, 0x00, 0xb5
    .byte 0x42, 0x40, 0x00, 0x2a, 0x00, 0xf0, 0x02, 0xf8
    .byte 0xf6, 0xd2, 0x8e, 0x46, 0x70, 0x46, 0x00, 0x47
zphd:

soft_float_table_size:
.byte (soft_float_table_end - soft_float_table) / 4
.align 2
soft_float_table:
    .word mufp_fadd
    .word mufp_fsub
    .word mufp_fmul
    .word mufp_fdiv
    .word mufp_fcmp_fast
    .word mufp_fcmp_fast_flags
    .word mufp_fsqrt
    .word mufp_float2int
    .word mufp_float2fix
    .word mufp_float2uint
    .word mufp_float2ufix
    .word mufp_int2float
    .word mufp_fix2float
    .word mufp_uint2float
    .word mufp_ufix2float
    .word mufp_fcos
    .word mufp_fsin
    .word mufp_ftan
    .word mufp_fsincos
    .word mufp_fexp
    .word mufp_fln

    .word mufp_fcmp_combined
    .word mufp_fatan2
    .word mufp_int642float
    .word mufp_fix642float
    .word mufp_uint642float
    .word mufp_ufix642float
    .word mufp_float2int64
    .word mufp_float2fix64
    .word mufp_float2uint64
    .word mufp_float2ufix64
    .word mufp_float2double
soft_float_table_end:

soft_double_table:
    .word mufp_dadd
    .word mufp_dsub
    .word mufp_dmul
    .word mufp_ddiv
    .word mufp_dcmp_fast
    .word mufp_dcmp_fast_flags
    .word mufp_dsqrt
    .word mufp_double2int
    .word mufp_double2fix
    .word mufp_double2uint
    .word mufp_double2ufix
    .word mufp_int2double
    .word mufp_fix2double
    .word mufp_uint2double
    .word mufp_ufix2double
    .word mufp_dcos
    .word mufp_dsin
    .word mufp_dtan
    .word mufp_dsincos
    .word mufp_dexp
    .word mufp_dln

    .word mufp_dcmp_combined
    .word mufp_datan2
    .word mufp_int642double
    .word mufp_fix642double
    .word mufp_uint642double
    .word mufp_ufix642double
    .word mufp_double2int64
    .word mufp_double2fix64
    .word mufp_double2uint64
    .word mufp_double2ufix64
    .word mufp_double2float
soft_double_table_end:
#if (soft_double_table_end - soft_double_table) != (soft_float_table_end - soft_float_table)
#error FLOAT and DOUBLE table size mismatch
#endif

#define USB_BOOT_STACK_SIZE 300

// we clear USB SRAM (aka .bss and stack), and switch stack
.global async_task_worker_thunk
.thumb_func
async_task_worker_thunk:
    // set stack
    ldr r0, =usb_boot_stack_end
    msr MSP, r0
    bl async_task_worker
    // async_task_worker does not return

_:

.section .bss
.align 2
usb_boot_stack:
.space USB_BOOT_STACK_SIZE * 4
usb_boot_stack_end:
