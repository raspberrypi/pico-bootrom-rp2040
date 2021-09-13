/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sys/param.h>
#include "runtime.h"
#include "program_flash_generic.h"
#include "async_task.h"
#include "usb_boot_device.h"
#include "usb_msc.h"
#include "boot/picoboot.h"
#include "hardware/sync.h"

//#define NO_ASYNC
//#define NO_ROM_READ

CU_REGISTER_DEBUG_PINS(flash)

static uint32_t _do_flash_enter_cmd_xip();
static uint32_t _do_flash_exit_xip();
static uint32_t _do_flash_erase_sector(uint32_t addr);
static uint32_t _do_flash_erase_range(uint32_t addr, uint32_t len);
static uint32_t _do_flash_page_program(uint32_t addr, uint8_t *data);
static uint32_t _do_flash_page_read(uint32_t addr, uint8_t *data);
static bool _is_address_safe_for_vectoring(uint32_t addr);

// keep table of flash function pointers in case RPI user wants to redirect them
static const struct flash_funcs {
    uint32_t size;
    uint32_t (*do_flash_enter_cmd_xip)();
    uint32_t (*do_flash_exit_xip)();
    uint32_t (*do_flash_erase_sector)();
    uint32_t (*do_flash_erase_range)(uint32_t addr, uint32_t size);
    uint32_t (*do_flash_page_program)(uint32_t addr, uint8_t *data);
    uint32_t (*do_flash_page_read)(uint32_t addr, uint8_t *data);
} default_flash_funcs = {
        .size = sizeof(struct flash_funcs),
        _do_flash_enter_cmd_xip,
        _do_flash_exit_xip,
        _do_flash_erase_sector,
        _do_flash_erase_range,
        _do_flash_page_program,
        _do_flash_page_read,
};

const struct flash_funcs *flash_funcs;

static uint32_t _do_flash_enter_cmd_xip() {
    usb_warn("flash ennter cmd XIP\n");
    flash_enter_cmd_xip();
    return 0;
}

static uint32_t _do_flash_exit_xip() {
    usb_warn("flash exit XIP\n");
    DEBUG_PINS_SET(flash, 2);
    connect_internal_flash();
    DEBUG_PINS_SET(flash, 4);
    flash_exit_xip();
    DEBUG_PINS_CLR(flash, 6);
#ifdef USE_BOOTROM_GPIO
    gpio_setup();
#endif
    return 0;
}

static uint32_t _do_flash_erase_sector(uint32_t addr) {
    usb_warn("erasing flash sector @%08x\n", (uint) addr);
    DEBUG_PINS_SET(flash, 2);
    flash_sector_erase(addr - XIP_MAIN_BASE);
    DEBUG_PINS_CLR(flash, 2);
    return 0;
}

static uint32_t _do_flash_erase_range(uint32_t addr, uint32_t len) {
    uint32_t end = addr + len;
    uint32_t ret = PICOBOOT_OK;
    while (addr < end && !ret) {
        ret = flash_funcs->do_flash_erase_sector(addr);
        addr += FLASH_SECTOR_ERASE_SIZE;
    }
    return ret;
}

static uint32_t _do_flash_page_program(uint32_t addr, uint8_t *data) {
    usb_warn("writing flash page @%08x\n", (uint) addr);
    DEBUG_PINS_SET(flash, 4);
    flash_page_program(addr - XIP_MAIN_BASE, data);
    DEBUG_PINS_CLR(flash, 4);
    // todo set error result
    return 0;
}

static uint32_t _do_flash_page_read(uint32_t addr, uint8_t *data) {
    DEBUG_PINS_SET(flash, 4);
    usb_warn("reading flash page @%08x\n", (uint) addr);
    flash_read_data(addr - XIP_MAIN_BASE, data, FLASH_PAGE_SIZE);
    DEBUG_PINS_CLR(flash, 4);
    // todo set error result
    return 0;
}

static bool _is_address_safe_for_vectoring(uint32_t addr) {
    // not we are inclusive at end to save arithmentic, and since we always checking for non empty ranges
    return is_address_ram(addr) &&
           (addr < FLASH_VALID_BLOCKS_BASE || addr > FLASH_VALID_BLOCKS_BASE + FLASH_BITMAPS_SIZE);
}

static uint8_t _last_mutation_source;

// NOTE for simplicity this returns error codes from PICOBOOT
static uint32_t _execute_task(struct async_task *task) {
    uint32_t ret;
    if (watchdog_rebooting()) {
        return PICOBOOT_REBOOTING;
    }
    uint type = task->type;
    if (type & AT_VECTORIZE_FLASH) {
        if (task->transfer_addr & 1u) {
            return PICOBOOT_BAD_ALIGNMENT;
        }
        if (_is_address_safe_for_vectoring(task->transfer_addr) &&
            _is_address_safe_for_vectoring(task->transfer_addr + sizeof(struct flash_funcs))) {
            memcpy((void *) task->transfer_addr, &default_flash_funcs, sizeof(struct flash_funcs));
            flash_funcs = (struct flash_funcs *) task->transfer_addr;
        } else {
            return PICOBOOT_INVALID_ADDRESS;
        }
    }
    if (type & AT_EXCLUSIVE) {
        // we do this in executex task, so we know we aren't executing and virtual_disk_queue tasks at this moment
        usb_warn("SET EXCLUSIVE ACCESS %d\n", task->exclusive_param);
        async_disable_queue(&virtual_disk_queue, task->exclusive_param);
        if (task->exclusive_param == EXCLUSIVE_AND_EJECT) {
            msc_eject();
        }
    }
    if (type & AT_EXIT_XIP) {
        ret = flash_funcs->do_flash_exit_xip();
        if (ret) return ret;
    }
    if (type & AT_EXEC) {
        usb_warn("exec %08x\n", (uint) task->transfer_addr);
        // scary but true; note callee must not overflow our stack (note also we reuse existing field task->transfer_addr to save code/data space)
        (((void (*)()) (task->transfer_addr | 1u)))();
    }
    if (type & (AT_WRITE | AT_FLASH_ERASE)) {
        if (task->check_last_mutation_source && _last_mutation_source != task->source) {
            return PICOBOOT_INTERLEAVED_WRITE;
        }
        _last_mutation_source = task->source;
    }
    if (type & AT_FLASH_ERASE) {
        usb_warn("request flash erase at %08x+%08x\n", (uint) task->erase_addr, (uint) task->erase_size);
        // todo maybe remove to save space
        if (task->erase_addr & (FLASH_SECTOR_ERASE_SIZE - 1)) return PICOBOOT_BAD_ALIGNMENT;
        if (task->erase_size & (FLASH_SECTOR_ERASE_SIZE - 1)) return PICOBOOT_BAD_ALIGNMENT;
        if (!(is_address_flash(task->erase_addr) && is_address_flash(task->erase_addr + task->erase_size))) {
            return PICOBOOT_INVALID_ADDRESS;
        }
        ret = flash_funcs->do_flash_erase_range(task->erase_addr, task->erase_size);
        if (ret) return ret;
    }
    bool direct_access = false;
    if (type & (AT_WRITE | AT_READ)) {
        if ((is_address_ram(task->transfer_addr) && is_address_ram(task->transfer_addr + task->data_length))
            #ifndef NO_ROM_READ
            || (!(type & AT_WRITE) && is_address_rom(task->transfer_addr) &&
                is_address_rom(task->transfer_addr + task->data_length))
#endif
                ) {
            direct_access = true;
        } else if ((is_address_flash(task->transfer_addr) &&
                    is_address_flash(task->transfer_addr + task->data_length))) {
            // flash
            if (task->transfer_addr & (FLASH_PAGE_SIZE - 1)) return PICOBOOT_BAD_ALIGNMENT;
        } else {
            // bad address
            return PICOBOOT_INVALID_ADDRESS;
        }
        if (type & AT_WRITE) {
            if (direct_access) {
                usb_warn("writing %08x +%04x\n", (uint) task->transfer_addr, (uint) task->data_length);
                uint32_t ff = (uintptr_t) flash_funcs;
                if (MAX(ff, task->transfer_addr) <
                    MIN(ff + sizeof(struct flash_funcs), task->transfer_addr + task->data_length)) {
                    usb_warn("RAM write overlaps vectors, reverting them to ROM\n");
                    flash_funcs = &default_flash_funcs;
                }
                memcpy((void *) task->transfer_addr, task->data, task->data_length);
            } else {
                assert(task->data_length <= FLASH_PAGE_SIZE);
                ret = flash_funcs->do_flash_page_program(task->transfer_addr, task->data);
                if (ret) return ret;
            }
        }
        if (type & AT_READ) {
            if (direct_access) {
                usb_warn("reading %08x +%04x\n", (uint) task->transfer_addr, (uint) task->data_length);
                memcpy(task->data, (void *) task->transfer_addr, task->data_length);
            } else {
                assert(task->data_length <= FLASH_PAGE_SIZE);
                ret = flash_funcs->do_flash_page_read(task->transfer_addr, task->data);
                if (ret) return ret;
            }
        }
        if (type & AT_ENTER_CMD_XIP) {
            ret = flash_funcs->do_flash_enter_cmd_xip();
            if (ret) return ret;
        }
    }
    return PICOBOOT_OK;
}

// just put this here in case it is worth noinlining - not atm
static void _task_copy(struct async_task *to, struct async_task *from) {
    //*to = *from;
    memcpy(to, from, sizeof(struct async_task));
}

void reset_task(struct async_task *task) {
    memset0(task, sizeof(struct async_task));
}

void queue_task(struct async_task_queue *queue, struct async_task *task, async_task_callback callback) {
    task->callback = callback;
#ifdef ASYNC_TASK_REQUIRE_TASK_CALLBACK
    assert(callback);
#endif
    assert(!task->result); // convention is that it is zero, so try to catch missing rest
#ifdef NO_ASYNC
    task->result = _execute_task(task);
    _call_task_complete(task);
#else
    if (queue->full) {
        usb_warn("overwriting already queued task for queue %p\n", queue);
    }
    _task_copy(&queue->task, task);
    queue->full = true;
    __sev();
#endif
}

static inline void _call_task_complete(struct async_task *task) {
#ifdef ASYNC_TASK_REQUIRE_TASK_CALLBACK
    task->callback(task);
#else
    if (task->callback) task->callback(task);
#endif
}

bool dequeue_task(struct async_task_queue *queue, struct async_task *task_out) {
#ifdef NO_ASYNC
    return false;
#else
    bool have_task = false;
    uint32_t save = save_and_disable_interrupts();
    __mem_fence_acquire();
    if (queue->full) {
        _task_copy(task_out, &queue->task);
        queue->full = false;
        have_task = true;
    }
    restore_interrupts(save);
    return have_task;
#endif
}

void execute_task(struct async_task_queue *queue, struct async_task *task) {
    if (queue->disable)
        task->result = 1; // todo better code (this is fine for now since we only ever disable virtual_disk queue which only cares where or not result is 0
    else
        task->result = _execute_task(task);
    uint32_t save = save_and_disable_interrupts();
    _call_task_complete(task);
    restore_interrupts(save);
}

struct async_task_queue virtual_disk_queue;

#ifndef NDEBUG
static bool _worker_started;
#endif

static struct async_task _worker_task;

void __attribute__((noreturn)) async_task_worker() {
    flash_funcs = &default_flash_funcs;
#ifndef NDEBUG
    _worker_started = true;
#endif
    do {
        if (dequeue_task(&virtual_disk_queue, &_worker_task)) {
            execute_task(&virtual_disk_queue, &_worker_task);
        }
#ifdef USE_PICOBOOT
        else if (dequeue_task(&picoboot_queue, &_worker_task)) {
            execute_task(&picoboot_queue, &_worker_task);
        }
#endif
        else {
            __wfe();
        }
    } while (true);
}
