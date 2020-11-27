/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _ASYNC_TASK_H
#define _ASYNC_TASK_H

// Simple async task without real locking... tasks execute in thread mode, they are queued by IRQs and state management and completion callbacks called with IRQs disabled
// which effectively means everything but the task execution is single threaded; as such the task should used async_task structure for all input/output
#include "runtime.h"

#define ASYNC_TASK_REQUIRE_TASK_CALLBACK
// bit field for task type
#define AT_EXIT_XIP         0x01u
#define AT_FLASH_ERASE      0x02u
#define AT_READ             0x04u
#define AT_WRITE            0x08u
#define AT_EXCLUSIVE        0x10u
#define AT_ENTER_CMD_XIP    0x20u
#define AT_EXEC             0x40u
#define AT_VECTORIZE_FLASH  0x80u

struct async_task;

typedef void (*async_task_callback)(struct async_task *task);

#define FLASH_PAGE_SIZE 256u
#define FLASH_PAGE_MASK (FLASH_PAGE_SIZE - 1u)
#define FLASH_SECTOR_ERASE_SIZE 4096u

enum task_source {
    TASK_SOURCE_VIRTUAL_DISK = 1,
    TASK_SOURCE_PICOBOOT,
};

// copy by value task definition
struct async_task {
    uint32_t token;
    uint32_t result;
    async_task_callback callback;

    // we only have one  task type now, so inlining
    uint32_t transfer_addr;
    uint32_t erase_addr;
    uint32_t erase_size;
    uint8_t *data;
    uint32_t data_length;
    uint32_t picoboot_user_token;
    uint8_t type;
    uint8_t exclusive_param;
    // an identifier for the logical source of the task
    uint8_t source;
    // if true, fail the task if the source isn't the same as the last source that did a mutation
    bool check_last_mutation_source;
};

// arguably a very short queue; there is only one up "next" item which is set by queue_task...
// attempt to queue multiple items will overwrite (so generally use multiple queues)
//
// this purely allows us to queue one task from the IRQ scope to the worker scope while
// that worker scope may still be executing the last one
struct async_task_queue {
    struct async_task task;
    volatile bool full;
    volatile bool disable;
};

// called by irq handler to queue a task
void queue_task(struct async_task_queue *queue, struct async_task *task, async_task_callback callback);

// called from thread to dequeue a task
bool dequeue_task(struct async_task_queue *queue, struct async_task *task_out);

// runs forever dispatch tasks
void __attribute__((noreturn)) async_task_worker();

void reset_task(struct async_task *task);

extern struct async_task_queue virtual_disk_queue;
extern struct async_task_queue picoboot_queue;

static inline void async_disable_queue(struct async_task_queue *queue, bool disable) {
    queue->disable = disable;
}

static inline void reset_queue(struct async_task_queue *queue) {
    queue->full = false;
    async_disable_queue(queue, false);
}

// async task needs to know where the flash bitmap is so it can avoid it
#ifndef USB_BOOT_EXPANDED_RUNTIME
#define FLASH_VALID_BLOCKS_BASE XIP_SRAM_BASE
#else
#define FLASH_VALID_BLOCKS_BASE (SRAM_BASE + 96 * 1024)
#endif
#define FLASH_BITMAPS_SIZE (XIP_SRAM_END - XIP_SRAM_BASE)

#endif //ASYNC_TASK_H_
