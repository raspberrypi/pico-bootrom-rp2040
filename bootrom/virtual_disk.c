/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "runtime.h"
#include "usb_boot_device.h"
#include "virtual_disk.h"
#include "boot/uf2.h"
#include "scsi.h"
#include "usb_msc.h"
#include "async_task.h"
#include "generated.h"

// Fri, 05 Sep 2008 16:20:51
#define RASPBERRY_PI_TIME_FRAC 100
#define RASPBERRY_PI_TIME ((16u << 11u) | (20u << 5u) | (51u >> 1u))
#define RASPBERRY_PI_DATE ((28u << 9u) | (9u << 5u) | (5u))
//#define NO_PARTITION_TABLE

#define CLUSTER_SIZE (4096u * CLUSTER_UP_MUL)
#define CLUSTER_SHIFT (3u + CLUSTER_UP_SHIFT)
static_assert(CLUSTER_SIZE == SECTOR_SIZE << CLUSTER_SHIFT, "");

#define CLUSTER_COUNT (VOLUME_SIZE / CLUSTER_SIZE)

static_assert(CLUSTER_COUNT <= 65526, "FAT16 limit");

#ifdef NO_PARTITION_TABLE
#define VOLUME_SECTOR_COUNT SECTOR_COUNT
#else
#define VOLUME_SECTOR_COUNT (SECTOR_COUNT-1)
#endif

#define FAT_COUNT 2u
#define MAX_ROOT_DIRECTORY_ENTRIES 512
#define ROOT_DIRECTORY_SECTORS (MAX_ROOT_DIRECTORY_ENTRIES * 32u / SECTOR_SIZE)

#define lsb_hword(x) (((uint)(x)) & 0xffu), ((((uint)(x))>>8u)&0xffu)
#define lsb_word(x) (((uint)(x)) & 0xffu), ((((uint)(x))>>8u)&0xffu),  ((((uint)(x))>>16u)&0xffu),  ((((uint)(x))>>24u)&0xffu)

#define SECTORS_PER_FAT (2 * (CLUSTER_COUNT + SECTOR_SIZE - 1) / SECTOR_SIZE)
static_assert(SECTORS_PER_FAT < 65536, "");

static_assert(VOLUME_SIZE >= 16 * 1024 * 1024, "volume too small for fat16");

// we are a hard drive - SCSI inquiry defines removability
#define IS_REMOVABLE_MEDIA false
#define MEDIA_TYPE (IS_REMOVABLE_MEDIA ? 0xf0u : 0xf8u)

#define MAX_RAM_UF2_BLOCKS 1280
static_assert(MAX_RAM_UF2_BLOCKS >= ((SRAM_END - SRAM_BASE) + (XIP_SRAM_END - XIP_SRAM_BASE)) / 256, "");

static __attribute__((aligned(4))) uint32_t uf2_valid_ram_blocks[(MAX_RAM_UF2_BLOCKS + 31) / 32];

enum partition_type {
    PT_FAT12 = 1,
    PT_FAT16 = 4,
    PT_FAT16_LBA = 0xe,
};

static const uint8_t boot_sector[] = {
        // 00 here should mean not bootable (according to spec) -- still windows unhappy without it
        0xeb, 0x3c, 0x90,
        // 03 id
        'M', 'S', 'W', 'I', 'N', '4', '.', '1',
//        'U', 'F', '2', ' ', 'U', 'F', '2', ' ',
        // 0b bytes per sector
        lsb_hword(512),
        // 0d sectors per cluster
        (CLUSTER_SIZE / SECTOR_SIZE),
        // 0e reserved sectors
        lsb_hword(1),
        // 10 fat count
        FAT_COUNT,
        // 11 max number root entries
        lsb_hword(MAX_ROOT_DIRECTORY_ENTRIES),
        // 13 number of sectors, if < 32768
#if VOLUME_SECTOR_COUNT < 65536
        lsb_hword(VOLUME_SECTOR_COUNT),
#else
        lsb_hword(0),
#endif
        // 15 media descriptor
        MEDIA_TYPE,
        // 16 sectors per FAT
        lsb_hword(SECTORS_PER_FAT),
        // 18 sectors per track (non LBA)
        lsb_hword(1),
        // 1a heads (non LBA)
        lsb_hword(1),
        // 1c hidden sectors 1 for MBR
        lsb_word(SECTOR_COUNT - VOLUME_SECTOR_COUNT),
// 20 sectors if >32K
#if VOLUME_SECTOR_COUNT >= 65536
        lsb_word(VOLUME_SECTOR_COUNT),
#else
        lsb_word(0),
#endif
        // 24 drive number
        0,
        // 25 reserved (seems to be chkdsk flag for clean unmount - linux writes 1)
        0,
        // 26 extended boot sig
        0x29,
        // 27 serial number
        0, 0, 0, 0,
        // 2b label
        'R', 'P', 'I', '-', 'R', 'P', '2', ' ', ' ', ' ', ' ',
        'F', 'A', 'T', '1', '6', ' ', ' ', ' ',
        0xeb, 0xfe // while(1);
};
static_assert(sizeof(boot_sector) == 0x40, "");

#define BOOT_OFFSET_SERIAL_NUMBER 0x27
#define BOOT_OFFSET_LABEL 0x2b

#define ATTR_READONLY       0x01u
#define ATTR_HIDDEN         0x02u
#define ATTR_SYSTEM         0x04u
#define ATTR_VOLUME_LABEL   0x08u
#define ATTR_DIR            0x10u
#define ATTR_ARCHIVE        0x20u

#define MBR_OFFSET_SERIAL_NUMBER 0x1b8

struct dir_entry {
    uint8_t name[11];
    uint8_t attr;
    uint8_t reserved;
    uint8_t creation_time_frac;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t cluster_hi;
    uint16_t last_modified_time;
    uint16_t last_modified_date;
    uint16_t cluster_lo;
    uint32_t size;
};

static_assert(sizeof(struct dir_entry) == 32, "");

static struct uf2_info {
    uint32_t *valid_blocks;
    uint32_t max_valid_blocks;
    uint32_t *cleared_pages;
    uint32_t max_cleared_pages;
    uint32_t num_blocks;
    uint32_t token;
    uint32_t valid_block_count;
    uint32_t lowest_addr;
    uint32_t block_no;
    struct async_task next_task;
    bool ram;
} _uf2_info;

// --- start non IRQ code ---

static void _write_uf2_page_complete(struct async_task *task) {
    if (task->token == _uf2_info.token) {
        if (!task->result && _uf2_info.valid_block_count == _uf2_info.num_blocks) {
            safe_reboot(_uf2_info.ram ? _uf2_info.lowest_addr : 0, SRAM_END, 1000); //300); // reboot in 300 ms
        }
    }
    vd_async_complete(task->token, task->result);
}

// return true for async
static bool _write_uf2_page() {
    // If we need to write a page (i.e. it hasn't been written before, then we queue a task to do that asynchronously
    //
    // Note that in an ideal world, given that we aren't synchronizing with the task in any way from here on,
    // we'd hand that task an immutable work item so that we don't step on the task's toes later.
    //
    // In the constrained bootrom (no RAM use) environment we don't have space to do that, so instead we pass
    // it a work item which is immutable except for the data buffer to be written.
    //
    // Note that we also pre-update all _uf2_info state in anticipation of the write being completed. This saves us
    // doing some extra figuring in _write_uf2_page_complete later, and there are only two cases we care about
    //
    // 1) that the task fails, in which case we'll notice in _write_uf2_page_complete anyway, and we can reset.
    // 2) that we superseded what the task was doing with a new UF2 download, in which case the old state is irrelevant.
    //
    // So basically the rule is, that this method (and _write_uf2_page_complete) which are both called under our
    // pseudo-lock (i.e. during IRQ or with IRQs disabled) are the onlu things that touch UF2 tracking state...
    // the task just takes an immutable command (with possibly mutable data), and takes care of writing that data to FLASH or RAM
    // along with erase etc.
    usb_debug("_write_uf2_page tok %d block %d / %d\n", (int) _uf2_info.token, _uf2_info.block_no,
              (int) _uf2_info.info.num_blocks);
    uint block_offset = _uf2_info.block_no / 32;
    uint32_t block_mask = 1u << (_uf2_info.block_no & 31u);
    if (!(_uf2_info.valid_blocks[block_offset] & block_mask)) {
        // note we don't want to pick XIP_CACHE over RAM even though it has a lower address
        bool xip_cache_next = _uf2_info.next_task.transfer_addr < SRAM_BASE;
        bool xip_cache_lowest = _uf2_info.lowest_addr < SRAM_BASE;
        if ((_uf2_info.next_task.transfer_addr < _uf2_info.lowest_addr && xip_cache_next == xip_cache_lowest) ||
            (xip_cache_lowest && !xip_cache_next)) {
            _uf2_info.lowest_addr = _uf2_info.next_task.transfer_addr;
        }
        if (_uf2_info.ram) {
            assert(_uf2_info.next_task.transfer_addr);
        } else {
            uint page_no = _uf2_info.block_no * 256 / FLASH_SECTOR_ERASE_SIZE;
            assert(_uf2_info.cleared_pages);
            assert(page_no < _uf2_info.max_cleared_pages);
            uint page_offset = page_no / 32;
            uint32_t page_mask = 1u << (page_no & 31u);
            assert(page_offset <= _uf2_info.max_cleared_pages);
            if (!(_uf2_info.cleared_pages[page_offset] & page_mask)) {
                _uf2_info.next_task.erase_addr = _uf2_info.next_task.transfer_addr & ~(FLASH_SECTOR_ERASE_SIZE - 1u);
                _uf2_info.next_task.erase_size = FLASH_SECTOR_ERASE_SIZE; // always erase a single sector
                usb_debug("Setting erase addr %08x\n", (uint) _uf2_info.next_task.erase_addr);
                _uf2_info.cleared_pages[page_offset] |= page_mask;
                _uf2_info.next_task.type |= AT_FLASH_ERASE;
            }
            usb_debug("Have flash destined page %08x (%08x %08x)\n", (uint) _uf2_info.next_task.transfer_addr,
                      (uint) *(uint32_t *) _uf2_info.next_task.data,
                      (uint) *(uint32_t *) (_uf2_info.next_task.data + 4));
            assert(!(_uf2_info.next_task.transfer_addr & 0xffu));
        }
        _uf2_info.valid_block_count++;
        _uf2_info.valid_blocks[block_offset] |= block_mask;
        usb_warn("Queuing 0x%08x->0x%08x valid %d/%d checked %d/%d\n", (uint)
                (uint) _uf2_info.next_task.transfer_addr, (uint) (_uf2_info.next_task.transfer_addr + FLASH_PAGE_SIZE),
                 (uint) _uf2_info.block_no + 1u, (uint) _uf2_info.num_blocks, (uint) _uf2_info.valid_block_count,
                 (uint) _uf2_info.num_blocks);
        queue_task(&virtual_disk_queue, &_uf2_info.next_task, _write_uf2_page_complete);
        // after the first write (i.e. next time, we want to check the source)
        _uf2_info.next_task.check_last_mutation_source = true;
        // note that queue_task may actually be handled sychronously based on #define, however that is OK
        // because it still calls _write_uf2_page_complete which still calls vd_async_complete which is allowed even in non async.
        return true;
    } else {
        assert(_uf2_info.next_task.type); // we should not have had any valid blocks after reset... we must take the above path so that the task gets executed
        uf2_debug("Ignore duplicate write to 0x%08x->0x%08x\n",
                  (uint) _uf2_info.next_task.transfer_addr,
                  (uint) (_uf2_info.next_task.transfer_addr + FLASH_PAGE_SIZE));
    }
    return false; // not async
}

void vd_init() {
}

void vd_reset() {
    usb_debug("Resetting virtual disk\n");
    _uf2_info.num_blocks = 0; // marker that uf2_info is invalid
}

// note caller must pass SECTOR_SIZE buffer
void init_dir_entry(struct dir_entry *entry, const char *fn, uint cluster, uint len) {
    entry->creation_time_frac = RASPBERRY_PI_TIME_FRAC;
    entry->creation_time = RASPBERRY_PI_TIME;
    entry->creation_date = RASPBERRY_PI_DATE;
    entry->last_modified_time = RASPBERRY_PI_TIME;
    entry->last_modified_date = RASPBERRY_PI_DATE;
    memcpy(entry->name, fn, 11);
    entry->attr = ATTR_READONLY | ATTR_ARCHIVE;
    entry->cluster_lo = cluster;
    entry->size = len;
}

bool vd_read_block(__unused uint32_t token, uint32_t lba, uint8_t *buf __comma_removed_for_space(uint32_t buf_size)) {
    assert(buf_size >= SECTOR_SIZE);
    memset0(buf, SECTOR_SIZE);
#ifndef NO_PARTITION_TABLE
    if (!lba) {
        uint8_t *ptable = buf + SECTOR_SIZE - 2 - 64;

#if 0
        // simple LBA partition at sector 1
        ptable[4] = PT_FAT16_LBA;
        // 08 LSB start sector
        ptable[8] = 1;
        // 12 LSB sector count
        ptable[12] = (SECTOR_COUNT-1) & 0xffu;
        ptable[13] = ((SECTOR_COUNT-1)>>8u) & 0xffu;
        ptable[14] = ((SECTOR_COUNT-1)>>16u) & 0xffu;
        static_assert(!(SECTOR_COUNT>>24u), "");
#else
        static_assert(!((SECTOR_COUNT - 1u) >> 24), "");
        static const uint8_t _ptable_data4[] = {
                PT_FAT16_LBA, 0, 0, 0,
                lsb_word(1), // sector 1
                // sector count, but we know the MS byte is zero
                (SECTOR_COUNT - 1u) & 0xffu,
                ((SECTOR_COUNT - 1u) >> 8u) & 0xffu,
                ((SECTOR_COUNT - 1u) >> 16u) & 0xffu,
        };
        memcpy(ptable + 4, _ptable_data4, sizeof(_ptable_data4));
#endif
        ptable[64] = 0x55;
        ptable[65] = 0xaa;

        uint32_t sn = msc_get_serial_number32();
        memcpy(buf + MBR_OFFSET_SERIAL_NUMBER, &sn, 4);
        return false;
    }
    lba--;
#endif
    if (!lba) {
        uint32_t sn = msc_get_serial_number32();
        memcpy(buf, boot_sector, sizeof(boot_sector));
        memcpy(buf + BOOT_OFFSET_SERIAL_NUMBER, &sn, 4);
    } else {
        lba--;
        if (lba < SECTORS_PER_FAT * FAT_COUNT) {
            // mirror
            while (lba >= SECTORS_PER_FAT) lba -= SECTORS_PER_FAT;
            if (!lba) {
                uint16_t *p = (uint16_t *) buf;
                p[0] = 0xff00u | MEDIA_TYPE;
                p[1] = 0xffff;
                p[2] = 0xffff; // cluster2 is index.htm
#ifdef USE_INFO_UF2
                p[3] = 0xffff; // cluster3 is info_uf2.txt
#endif
            }
        } else {
            lba -= SECTORS_PER_FAT * FAT_COUNT;
            if (lba < ROOT_DIRECTORY_SECTORS) {
                // we don't support that many directory entries actually
                if (!lba) {
                    // root directory
                    struct dir_entry *entries = (struct dir_entry *) buf;
                    memcpy(entries[0].name, (boot_sector + BOOT_OFFSET_LABEL), 11);
                    entries[0].attr = ATTR_VOLUME_LABEL | ATTR_ARCHIVE;
                    init_dir_entry(++entries, "INDEX   HTM", 2, welcome_html_len);
#ifdef USE_INFO_UF2
                    init_dir_entry(++entries, "INFO_UF2TXT", 3, info_uf2_txt_len);
#endif
                }
            } else {
                lba -= ROOT_DIRECTORY_SECTORS;
                uint cluster = lba >> CLUSTER_SHIFT;
                uint cluster_offset = lba - (cluster << CLUSTER_SHIFT);
                if (!cluster_offset) {
                    if (cluster == 0) {
#ifndef COMPRESS_TEXT
                        memcpy(buf, welcome_html, welcome_html_len);
#else
                        poor_mans_text_decompress(welcome_html_z + sizeof(welcome_html_z), sizeof(welcome_html_z), buf);
                        memcpy(buf + welcome_html_version_offset_1, serial_number_string, 12);
                        memcpy(buf + welcome_html_version_offset_2, serial_number_string, 12);
#endif
                    }
#ifdef USE_INFO_UF2
                    else if (cluster == 1) {
                        // spec suggests we have this as raw text in the binary, although it doesn't much matter if no CURRENT.UF2 file
                        // note that this text doesn't compress anyway, so do this raw anyway
                        memcpy(buf, info_uf2_txt, info_uf2_txt_len);
                    }
#endif
                }
            }
        }
    }
    return false;
}

#define FLASH_MAX_VALID_BLOCKS ((FLASH_BITMAPS_SIZE * 8LL * FLASH_SECTOR_ERASE_SIZE / (FLASH_PAGE_SIZE + FLASH_SECTOR_ERASE_SIZE)) & ~31u)
#define FLASH_CLEARED_PAGES_BASE (FLASH_VALID_BLOCKS_BASE + FLASH_MAX_VALID_BLOCKS / 8)
static_assert(!(FLASH_CLEARED_PAGES_BASE & 0x3), "");
#define FLASH_MAX_CLEARED_PAGES (FLASH_MAX_VALID_BLOCKS * FLASH_PAGE_SIZE / FLASH_SECTOR_ERASE_SIZE)
static_assert(FLASH_CLEARED_PAGES_BASE + (FLASH_MAX_CLEARED_PAGES / 32 - FLASH_VALID_BLOCKS_BASE <= FLASH_BITMAPS_SIZE),
              "");

static void _clear_bitset(uint32_t *mask, uint32_t count) {
    memset0(mask, count / 8);
}

static bool _update_current_uf2_info(struct uf2_block *uf2, uint32_t token) {
    bool ram = is_address_ram(uf2->target_addr) && is_address_ram(uf2->target_addr + (FLASH_PAGE_MASK));
    bool flash = is_address_flash(uf2->target_addr) && is_address_flash(uf2->target_addr + (FLASH_PAGE_MASK));
    if (!(uf2->num_blocks && (ram || flash)) || (flash && (uf2->target_addr & (FLASH_PAGE_MASK)))) {
        uf2_debug("Resetting active UF2 transfer because received garbage\n");
    } else if (!virtual_disk_queue.disable) {
        // note (test abive) if virtual disk queue is disabled (and note since we're in IRQ that cannot change whilst we are executing),
        // then we don't want to do any of this even if the task will be ignored later (doing this would modify our state)
        uint8_t type = AT_WRITE; // we always write
        if (_uf2_info.num_blocks != uf2->num_blocks) {
            // todo we may be able to skip some of these checks and let the task handle it (it will ignore garbage addresses for example)
            uf2_debug("Resetting active UF2 transfer because have new binary size %d->%d\n", (int) _uf2_info.num_blocks,
                      (int) uf2->num_blocks);
            memset0(&_uf2_info, sizeof(_uf2_info));
            _uf2_info.ram = ram;
            _uf2_info.valid_blocks = ram ? uf2_valid_ram_blocks : (uint32_t *) FLASH_VALID_BLOCKS_BASE;
            _uf2_info.max_valid_blocks = ram ? count_of(uf2_valid_ram_blocks) * 32 : FLASH_MAX_VALID_BLOCKS;
            uf2_debug("  ram %d, so valid_blocks (max %d) %p->%p for %dK\n", ram, (int) _uf2_info.max_valid_blocks,
                      _uf2_info.valid_blocks, _uf2_info.valid_blocks + ((_uf2_info.max_valid_blocks + 31) / 32),
                      (uint) _uf2_info.max_valid_blocks / 4);
            _clear_bitset(_uf2_info.valid_blocks, _uf2_info.max_valid_blocks);
            if (flash) {
                _uf2_info.cleared_pages = (uint32_t *) FLASH_CLEARED_PAGES_BASE;
                _uf2_info.max_cleared_pages = FLASH_MAX_CLEARED_PAGES;
                uf2_debug("    cleared_pages %p->%p\n", _uf2_info.cleared_pages,
                          _uf2_info.cleared_pages + ((_uf2_info.max_cleared_pages + 31) / 32));
                _clear_bitset(_uf2_info.cleared_pages, _uf2_info.max_cleared_pages);
            }

            if (uf2->num_blocks > _uf2_info.max_valid_blocks) {
                uf2_debug("Oops image requires %d blocks and won't fit", (uint) uf2->num_blocks);
                return false;
            }
            usb_warn("New UF2 transfer\n");
            _uf2_info.num_blocks = uf2->num_blocks;
            _uf2_info.valid_block_count = 0;
            _uf2_info.lowest_addr = 0xffffffff;
            if (flash) type |= AT_EXIT_XIP;
        }
        if (ram != _uf2_info.ram) {
            uf2_debug("Ignoring write to out of range address 0x%08x->0x%08x\n",
                      (uint) uf2->target_addr, (uint) (uf2->target_addr + uf2->payload_size));
        } else {
            assert(uf2->num_blocks <= _uf2_info.max_valid_blocks);
            if (uf2->block_no < uf2->num_blocks) {
                // set up next task state (also serves as a holder for state scoped to this block write to avoid copying data around)
                reset_task(&_uf2_info.next_task);
                _uf2_info.block_no = uf2->block_no;
                _uf2_info.token = _uf2_info.next_task.token = token;
                _uf2_info.next_task.transfer_addr = uf2->target_addr;
                _uf2_info.next_task.type = type;
                _uf2_info.next_task.data = uf2->data;
                _uf2_info.next_task.callback = _write_uf2_page_complete;
                _uf2_info.next_task.data_length = FLASH_PAGE_SIZE; // always a full page
                _uf2_info.next_task.source = TASK_SOURCE_VIRTUAL_DISK;
                return true;
            } else {
                uf2_debug("Ignoring write to out of range block %d >= %d\n", (int) uf2->block_no,
                          (int) uf2->num_blocks);
            }
        }
    }
    _uf2_info.num_blocks = 0; // invalid
    return false;
}

// note caller must pass SECTOR_SIZE buffer
bool vd_write_block(uint32_t token, __unused uint32_t lba, uint8_t *buf __comma_removed_for_space(uint32_t buf_size)) {
    struct uf2_block *uf2 = (struct uf2_block *) buf;
    if (uf2->magic_start0 == UF2_MAGIC_START0 && uf2->magic_start1 == UF2_MAGIC_START1 &&
        uf2->magic_end == UF2_MAGIC_END) {
        if (uf2->flags & UF2_FLAG_FAMILY_ID_PRESENT && uf2->file_size == RP2040_FAMILY_ID &&
            !(uf2->flags & UF2_FLAG_NOT_MAIN_FLASH) && uf2->payload_size == 256) {
            if (_update_current_uf2_info(uf2, token)) {
                // if we have a valid uf2 page, write it
                return _write_uf2_page();
            }
        } else {
            uf2_debug("Sector %d: ignoring write of non Mu UF2 sector\n", (uint) lba);
        }
    } else {
        uf2_debug("Sector %d: ignoring write of non UF2 sector\n", (uint) lba);
    }
    return false;
}