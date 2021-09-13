/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "boot/picoboot.h"
#include "runtime.h"
#include "usb_device.h"
#include "async_task.h"
#include "program_flash_generic.h"
#include "usb_boot_device.h"
#include "usb_msc.h"
#include "usb_stream_helper.h"
#include "hardware/regs/sysinfo.h"

// mutable serial number string for us to initialize on startup
char serial_number_string[13];

// todo make descriptor strings should probably belong/come from the configs
static char *descriptor_strings[] =
        {
                "",
                "Raspberry Pi",
                "RP2 Boot",
                serial_number_string
        };

struct usb_simple_interface_descriptor {
    struct usb_interface_descriptor desc;
    struct usb_endpoint_descriptor ep1_desc;
    struct usb_endpoint_descriptor ep2_desc;
} __packed;

#ifdef USE_PICOBOOT
#define BOOT_DEVICE_NUM_INTERFACES 2
#else
#define BOOT_DEVICE_NUM_INTERFACES 1
#endif

struct boot_device_config {
    struct usb_configuration_descriptor config_desc;
    struct usb_simple_interface_descriptor interface_desc[BOOT_DEVICE_NUM_INTERFACES];
} __packed;

static const struct boot_device_config boot_device_config = {
        .config_desc = {
                .bLength             = 0x09,    // Descriptor size is 9 bytes
                .bDescriptorType     = 0x02,   // CONFIGURATION Descriptor Type
                .wTotalLength        = sizeof(boot_device_config),
                .bNumInterfaces      = BOOT_DEVICE_NUM_INTERFACES,
                .bConfigurationValue = 0x01,   // The value 1 should be used to select this configuration
                .iConfiguration      = 0x00,   // The device doesn't have the string descriptor describing this configuration
                .bmAttributes        = 0x80,   // Configuration characteristics : Bit 7: Reserved (set to one) 1 Bit 6: Self-powered 0 Bit 5: Remote Wakeup 0
                .bMaxPower           = 0xfa,   // Maximum power consumption of the device in this configuration is 500 mA
        },
        .interface_desc = {
                {
                        .desc = {
                                .bLength            = 0x09, // Descriptor size is 9 bytes
                                .bDescriptorType    = 0x04, // INTERFACE Descriptor Type
                                .bInterfaceNumber   = 0x00, // The number of this interface is 0.
                                .bAlternateSetting  = 0x00, // The value used to select the alternate setting for this interface is 0
                                .bNumEndpoints      = 0x02,
                                .bInterfaceClass    = 0x08, // The interface implements the Mass Storage class
                                .bInterfaceSubClass = 0x06, // The interface implements the SCSI Transparent Subclass
                                .bInterfaceProtocol = 0x50, // The interface uses the Bulk-Only Protocol
                                .iInterface         = 0x00, // The device doesn't have a string descriptor describing this iInterface
                        },
                        .ep1_desc = {
                                .bLength          = 0x07,   // Descriptor size is 7 bytes
                                .bDescriptorType  = 0x05,   // ENDPOINT Descriptor Type
                                .bEndpointAddress = 0x81,   // This is an IN endpoint with endpoint number 1
                                .bmAttributes     = 0x02,   // Types - BULK
                                .wMaxPacketSize   = 0x0040, // Maximum packet size for this endpoint is 64 Bytes. If Hi-Speed, 0 additional transactions per frame
                                .bInterval        = 0x00,   // The polling interval value is every 0 Frames. Undefined for Hi-Speed
                        },
                        .ep2_desc = {
                                .bLength          = 0x07,   // Descriptor size is 7 bytes
                                .bDescriptorType  = 0x05,   // ENDPOINT Descriptor Type
                                .bEndpointAddress = 0x02,   // This is an OUT endpoint with endpoint number 2
                                .bmAttributes     = 0x02,   // Types - BULK
                                .wMaxPacketSize   = 0x0040, // Maximum packet size for this endpoint is 64 Bytes. If Hi-Speed, 0 additional transactions per frame
                                .bInterval        = 0x00,   // The polling interval value is every 0 Frames. If Hi-Speed, 0 uFrames/NAK
                        },
                },
#ifdef USE_PICOBOOT
                {
                        .desc = {
                                .bLength            = 0x09, // Descriptor size is 9 bytes
                                .bDescriptorType    = 0x04, // INTERFACE Descriptor Type
                                .bInterfaceNumber   = 0x01, // The number of this interface is 1.
                                .bAlternateSetting  = 0x00, // The value used to select the alternate setting for this interface is 0
                                .bNumEndpoints      = 0x02,
                                .bInterfaceClass    = 0xff, // The interface is vendor specific
                                .bInterfaceSubClass = 0x00, // no subclass
                                .bInterfaceProtocol = 0x00, // no protocol
                                .iInterface         = 0x00, // The device doesn't have a string descriptor describing this iInterface
                        },
                        .ep1_desc = {
                                .bLength          = 0x07,   // Descriptor size is 7 bytes
                                .bDescriptorType  = 0x05,   // ENDPOINT Descriptor Type
                                .bEndpointAddress = 0x03,   // This is an OUT endpoint with endpoint number 3
                                .bmAttributes     = 0x02,   // Types - BULK
                                .wMaxPacketSize   = 0x0040, // Maximum packet size for this endpoint is 64 Bytes. If Hi-Speed, 0 additional transactions per frame
                                .bInterval        = 0x00,   // The polling interval value is every 0 Frames. If Hi-Speed, 0 uFrames/NAK
                        },
                        .ep2_desc = {
                                .bLength          = 0x07,   // Descriptor size is 7 bytes
                                .bDescriptorType  = 0x05,   // ENDPOINT Descriptor Type
                                .bEndpointAddress = 0x84,   // This is an IN endpoint with endpoint number 4
                                .bmAttributes     = 0x02,   // Types - BULK
                                .wMaxPacketSize   = 0x0040, // Maximum packet size for this endpoint is 64 Bytes. If Hi-Speed, 0 additional transactions per frame
                                .bInterval        = 0x00,   // The polling interval value is every 0 Frames. Undefined for Hi-Speed
                        }
                }
#endif
        }
};

static_assert(sizeof(boot_device_config) == sizeof(struct usb_configuration_descriptor) +
                                            BOOT_DEVICE_NUM_INTERFACES * sizeof(struct usb_simple_interface_descriptor),
              "");

static struct usb_interface msd_interface;

#ifdef USE_PICOBOOT
static struct usb_endpoint picoboot_in, picoboot_out;
static struct usb_interface picoboot_interface;
#endif

static const struct usb_device_descriptor boot_device_descriptor = {
        .bLength            = 18, // Descriptor size is 18 bytes
        .bDescriptorType    = 0x01, // DEVICE Descriptor Type
        .bcdUSB             = 0x0110, // USB Specification version 1.1
        .bDeviceClass       = 0x00, // Each interface specifies its own class information
        .bDeviceSubClass    = 0x00, // Each interface specifies its own Subclass information
        .bDeviceProtocol    = 0x00, // No protocols the device basis
        .bMaxPacketSize0    = 0x40, // Maximum packet size for endpoint zero is 64
        .idVendor           = VENDOR_ID,
        .idProduct          = PRODUCT_ID,
        .bcdDevice          = 0x0100, // The device release number is 1.00
        .iManufacturer      = 0x01, // The manufacturer string descriptor index is 1
        .iProduct           = 0x02, // The product string descriptor index is 2
        .iSerialNumber      = 3,//count_of(descriptor_strings) + 1, // The serial number string descriptor index is 3
        .bNumConfigurations = 0x01, // The device has 1 possible configurations
};

struct usb_device boot_device;

// note serial number must use upper case characters according to USB MSC spec
#define to_hex_digit(x) ((x) < 10 ? '0' + (x) : 'A' + (x) - 10)

static struct {
    uint32_t serial_number32;
    bool serial_number_valid;
} boot_device_state;

uint32_t msc_get_serial_number32() {
    if (!boot_device_state.serial_number_valid) {
        boot_device_state.serial_number32 = time_us_32();
        boot_device_state.serial_number_valid = true;
    }
    return boot_device_state.serial_number32;
}

const char *_get_descriptor_string(uint index) {
    if (index >= count_of(descriptor_strings)) index = 0;
    return descriptor_strings[index];
}


#ifdef USE_PICOBOOT

__rom_function_static_impl(void, _picoboot_cmd_packet)(struct usb_endpoint *ep);

static const struct usb_transfer_type _picoboot_cmd_transfer_type = {
        .on_packet = __rom_function_ref(_picoboot_cmd_packet),
        .initial_packet_count = 1,
};

struct picoboot_cmd_status _picoboot_current_cmd_status;

static void _picoboot_reset() {
    usb_debug("PICOBOOT RESET\n");
    usb_soft_reset_endpoint(&picoboot_out);
    usb_soft_reset_endpoint(&picoboot_in);
    if (_picoboot_current_cmd_status.bInProgress) {
        printf("command in progress so aborting flash\n");
        flash_abort();
    }
    memset0(&_picoboot_current_cmd_status, sizeof(_picoboot_current_cmd_status));
    // reset queue (note this also clears exclusive access)
    reset_queue(&virtual_disk_queue);
    reset_queue(&picoboot_queue);
}

struct async_task_queue picoboot_queue;

static void _tf_picoboot_wait_command(__unused struct usb_endpoint *ep, __unused struct usb_transfer *transfer) {
    usb_debug("_tf_picoboot_wait_command\n");
    // todo check this at the end of an OUT ACK
    usb_start_default_transfer_if_not_already_running_or_halted(&picoboot_out);
}

static void _picoboot_ack() {
    static struct usb_transfer _ack_transfer;
    _picoboot_current_cmd_status.bInProgress = false;
    usb_start_empty_transfer((_picoboot_current_cmd_status.bCmdId & 0x80u) ? &picoboot_out : &picoboot_in, &_ack_transfer,
                             _tf_picoboot_wait_command);
}

#define _tf_ack ((usb_transfer_completed_func)_picoboot_ack)

static bool _picoboot_setup_request_handler(__unused struct usb_interface *interface, struct usb_setup_packet *setup) {
    setup = __builtin_assume_aligned(setup, 4);
    if (USB_REQ_TYPE_TYPE_VENDOR == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK)) {
        if (setup->bmRequestType & USB_DIR_IN) {
            if (setup->bRequest == PICOBOOT_IF_CMD_STATUS && setup->wLength == sizeof(_picoboot_current_cmd_status)) {
                uint8_t *buffer = usb_get_single_packet_response_buffer(usb_get_control_in_endpoint(),
                                                                        sizeof(_picoboot_current_cmd_status));
                memcpy(buffer, &_picoboot_current_cmd_status, sizeof(_picoboot_current_cmd_status));
                usb_start_single_buffer_control_in_transfer();
                return true;
            }
        } else {
            if (setup->bRequest == PICOBOOT_IF_RESET) {
                _picoboot_reset();
                usb_start_empty_control_in_transfer_null_completion();
                return true;
            }
        }
    }
    return false;
}

static struct picoboot_stream_transfer {
    struct usb_stream_transfer stream;
    struct async_task task;
} _picoboot_stream_transfer;

static void _atc_ack(struct async_task *task) {
    if (task->picoboot_user_token == _picoboot_stream_transfer.task.picoboot_user_token) {
        usb_warn("atc_ack\n");
        _picoboot_ack();
    } else {
        usb_warn("atc for wrong picoboot token %08x != %08x\n", (uint) task->picoboot_user_token,
                 (uint) _picoboot_stream_transfer.task.picoboot_user_token);
    }
}

static void _set_cmd_status(uint32_t status) {
    _picoboot_current_cmd_status.dStatusCode = status;
}

static void _atc_chunk_task_done(struct async_task *task) {
    if (task->picoboot_user_token == _picoboot_stream_transfer.task.picoboot_user_token) {
        // save away result
        _set_cmd_status(task->result);
        if (task->result) {
            usb_halt_endpoint(_picoboot_stream_transfer.stream.ep);
            _picoboot_current_cmd_status.bInProgress = false;
        }
        // we update the position of the original task which will be submitted again in on_stream_chunk
        _picoboot_stream_transfer.task.transfer_addr += task->data_length;
        usb_stream_chunk_done(&_picoboot_stream_transfer.stream);
    }
}

__rom_function_static_impl(bool, _picoboot_on_stream_chunk)(uint32_t chunk_len __comma_removed_for_space(
        struct usb_stream_transfer *transfer)) {
    assert(transfer == &_picoboot_stream_transfer.stream);
    assert(chunk_len <= FLASH_PAGE_SIZE);
    _picoboot_stream_transfer.task.data_length = chunk_len;
    queue_task(&picoboot_queue, &_picoboot_stream_transfer.task, _atc_chunk_task_done);
    // for subsequent tasks, check the mutation source
    _picoboot_stream_transfer.task.check_last_mutation_source = true;
    return true;
}

static void _picoboot_cmd_packet_internal(struct usb_endpoint *ep) {
    struct usb_buffer *buffer = usb_current_out_packet_buffer(ep);
    uint len = buffer->data_len;

    struct picoboot_cmd *cmd = (struct picoboot_cmd *) buffer->data;
    if (len == sizeof(struct picoboot_cmd) && cmd->dMagic == PICOBOOT_MAGIC) {
        // pre-init even if we don't need it
        static uint32_t real_token;
        reset_task(&_picoboot_stream_transfer.task);
        _picoboot_stream_transfer.task.token = --real_token; // we go backwards to disambiguate with MSC tasks
        _picoboot_stream_transfer.task.picoboot_user_token = cmd->dToken;
        _picoboot_current_cmd_status.bCmdId = cmd->bCmdId;
        _picoboot_current_cmd_status.dToken = cmd->dToken;
        _picoboot_current_cmd_status.bInProgress = false;
        _set_cmd_status(PICOBOOT_UNKNOWN_CMD);
        _picoboot_stream_transfer.task.transfer_addr = _picoboot_stream_transfer.task.erase_addr = cmd->range_cmd.dAddr;
        _picoboot_stream_transfer.task.erase_size = cmd->range_cmd.dSize;
        _picoboot_stream_transfer.task.exclusive_param = cmd->exclusive_cmd.bExclusive;
        static_assert(
                offsetof(struct picoboot_cmd, range_cmd.dAddr) == offsetof(struct picoboot_cmd, address_only_cmd.dAddr),
                ""); // we want transfer_addr == exec_cmd.addr also
        uint type = 0;
        static_assert(1u == (PC_EXCLUSIVE_ACCESS & 0xfu), "");
        static_assert(2u == (PC_REBOOT & 0xfu), "");
        static_assert(3u == (PC_FLASH_ERASE & 0xfu), "");
        static_assert(4u == (PC_READ & 0xfu), "");
        static_assert(5u == (PC_WRITE & 0xfu), "");
        static_assert(6u == (PC_EXIT_XIP & 0xfu), "");
        static_assert(7u == (PC_ENTER_CMD_XIP & 0xfu), "");
        static_assert(8u == (PC_EXEC & 0xfu), "");
        static_assert(9u == (PC_VECTORIZE_FLASH & 0xfu), "");
        static uint8_t cmd_mapping[] = {
                0, 0, 0,
                sizeof(struct picoboot_exclusive_cmd), 0x00, AT_EXCLUSIVE,
                sizeof(struct picoboot_reboot_cmd), 0x00, 0, // reboot checked separately
                sizeof(struct picoboot_range_cmd), 0x00, AT_FLASH_ERASE,
                sizeof(struct picoboot_range_cmd), 0x80, AT_READ,
                sizeof(struct picoboot_range_cmd), 0x80, AT_WRITE,
                0, 0x00, AT_EXIT_XIP,
                0, 0x00, AT_ENTER_CMD_XIP,
                sizeof(struct picoboot_address_only_cmd), 0x00, AT_EXEC,
                sizeof(struct picoboot_address_only_cmd), 0x00, AT_VECTORIZE_FLASH
        };
        uint id = cmd->bCmdId & 0x7fu;
        if (id && id < count_of(cmd_mapping) / 3) {
            id *= 3;
            if (cmd->bCmdSize == cmd_mapping[id]) {
                _set_cmd_status(PICOBOOT_OK);
                uint32_t l = cmd_mapping[id + 1];
                if (l & 0x80u) {
                    l = cmd->range_cmd.dSize;
                }
                if (l == cmd->dTransferLength) {
                    type = cmd_mapping[id + 2];
                }
                if (cmd->bCmdId == PC_REBOOT) {
                    safe_reboot(cmd->reboot_cmd.dPC, cmd->reboot_cmd.dSP, cmd->reboot_cmd.dDelayMS);
                    return _picoboot_ack();
                }
                if (type) {
                    _picoboot_stream_transfer.task.type = type;
                    _picoboot_stream_transfer.task.source = TASK_SOURCE_PICOBOOT;
                    _picoboot_current_cmd_status.bInProgress = true;
                    if (cmd->dTransferLength) {
                        static uint8_t _buffer[FLASH_PAGE_SIZE];
                        static const struct usb_stream_transfer_funcs _picoboot_stream_funcs = {
                                .on_packet_complete = usb_stream_noop_on_packet_complete,
                                .on_chunk = __rom_function_ref(_picoboot_on_stream_chunk)
                        };

                        _picoboot_stream_transfer.task.data = _buffer;
                        usb_stream_setup_transfer(&_picoboot_stream_transfer.stream,
                                                  &_picoboot_stream_funcs, _buffer, FLASH_PAGE_SIZE,
                                                  cmd->dTransferLength,
                                                  _tf_ack);
                        if (type & AT_WRITE) {
                            _picoboot_stream_transfer.stream.ep = &picoboot_out;
                            return usb_chain_transfer(&picoboot_out, &_picoboot_stream_transfer.stream.core);
                        } else {
                            _picoboot_stream_transfer.stream.ep = &picoboot_in;
                            return usb_start_transfer(&picoboot_in, &_picoboot_stream_transfer.stream.core);
                        }
                    }
                    return queue_task(&picoboot_queue, &_picoboot_stream_transfer.task, _atc_ack);
                }
                _set_cmd_status(PICOBOOT_INVALID_TRANSFER_LENGTH);
            } else {
                _set_cmd_status(PICOBOOT_INVALID_CMD_LENGTH);
            }
        }
    }
    usb_halt_endpoint(&picoboot_in);
    usb_halt_endpoint(&picoboot_out);
}

__rom_function_static_impl(void, _picoboot_cmd_packet)(struct usb_endpoint *ep) {
    _picoboot_cmd_packet_internal(ep);
    usb_packet_done(ep);
}

#endif

static void _usb_boot_on_configure(struct usb_device *device, bool configured) {
#if !defined(USB_BOOT_EXPANDED_RUNTIME) || defined(NO_FLASH) // can't do this on a flash based usb_boot_test build for obvious reasons!
    // kill any in process flash which might be stuck - this will leave flash in bad state
    usb_warn("FLASH ABORT\n");
    flash_abort();
#endif
    msc_on_configure(device, configured);
#ifdef USE_PICOBOOT
    if (configured) _picoboot_reset();
#endif
}

static void _write_six_msb_hex_chars(char *dest, uint32_t val) {
    for (int i = 6; i > 0; i--) {
        uint x = val >> 28u;
        *dest++ = to_hex_digit(x);
        val <<= 4u;
    }
}

#ifdef USB_BOOT_WITH_SUBSET_OF_INTERFACES
static struct single_interface_boot_device_config {
    struct usb_configuration_descriptor config_desc;
    struct usb_simple_interface_descriptor interface_desc[1];
} _single_interface_config;
#endif

void usb_boot_device_init(uint32_t _usb_disable_interface_mask) {
#ifdef USB_BOOT_WITH_SUBSET_OF_INTERFACES
    uint32_t usb_disable_interface_mask = _usb_disable_interface_mask;
    if ((usb_disable_interface_mask & 3u) == 3u)
        usb_disable_interface_mask = 0; // bad things happen if we try to disable both
#else
    const uint32_t usb_disable_interface_mask = 0;
#endif
#ifdef USE_BOOTROM_GPIO
    gpio_setup();
#endif

    // set serial number
#ifndef USB_BOOT_EXPANDED_RUNTIME
    extern uint32_t software_git_revision;
#else
    uint32_t software_git_revision = 0xaaaaaaaa;
#endif
    _write_six_msb_hex_chars(serial_number_string, *(uint32_t *) (SYSINFO_BASE + SYSINFO_GITREF_RP2040_OFFSET));
    _write_six_msb_hex_chars(serial_number_string + 6, software_git_revision);

    const struct boot_device_config *config_desc = &boot_device_config;
    uint picoboot_interface_num = 1;
#ifdef USB_BOOT_WITH_SUBSET_OF_INTERFACES
    // if we are disabling interfaces
    if (usb_disable_interface_mask) {
        // copy descriptor and MSC descriptor
        memcpy(&_single_interface_config, &boot_device_config, sizeof(_single_interface_config));
        _single_interface_config.config_desc.wTotalLength = sizeof(_single_interface_config);
        static_assert(sizeof(_single_interface_config) ==
                      sizeof(struct usb_configuration_descriptor) + sizeof(struct usb_simple_interface_descriptor), "");
        if (usb_disable_interface_mask & 1u) {
            picoboot_interface_num = 0;
            memcpy(&_single_interface_config.interface_desc[0], &boot_device_config.interface_desc[1],
                   sizeof(struct usb_simple_interface_descriptor));
        }
        _single_interface_config.config_desc.bNumInterfaces = 1u;
        _single_interface_config.interface_desc[0].desc.bInterfaceNumber = 0;
        config_desc = (const struct boot_device_config *) &_single_interface_config;
    }
#endif
    if (!(usb_disable_interface_mask & 1u)) {
        static struct usb_endpoint *const _msc_endpoints[] = {
                msc_endpoints,
                msc_endpoints + 1
        };
        usb_interface_init(&msd_interface, &config_desc->interface_desc[0].desc, _msc_endpoints,
                           count_of(msc_endpoints), true);
        msd_interface.setup_request_handler = msc_setup_request_handler;
    }
#ifdef USE_PICOBOOT
    if (!(usb_disable_interface_mask & 2u)) {
        static struct usb_endpoint *const picoboot_endpoints[] = {
                &picoboot_out,
                &picoboot_in,
        };
        usb_interface_init(&picoboot_interface, &config_desc->interface_desc[picoboot_interface_num].desc,
                           picoboot_endpoints, count_of(picoboot_endpoints), true);
        static struct usb_transfer _picoboot_cmd_transfer;
        _picoboot_cmd_transfer.type = &_picoboot_cmd_transfer_type;
        usb_set_default_transfer(&picoboot_out, &_picoboot_cmd_transfer);
        picoboot_interface.setup_request_handler = _picoboot_setup_request_handler;
    }
#endif

    static struct usb_interface *const boot_device_interfaces[] = {
            &msd_interface,
#ifdef USE_PICOBOOT
            &picoboot_interface,
#endif
    };
    static_assert(count_of(boot_device_interfaces) == BOOT_DEVICE_NUM_INTERFACES, "");
    __unused struct usb_device *device = usb_device_init(&boot_device_descriptor, &config_desc->config_desc,
                                                         boot_device_interfaces + (usb_disable_interface_mask == 1),
                                                         usb_disable_interface_mask ? 1 : BOOT_DEVICE_NUM_INTERFACES,
                                                         _get_descriptor_string);
    assert(device);
    device->on_configure = _usb_boot_on_configure;
    usb_device_start();
}

uint8_t *usb_get_single_packet_response_buffer(struct usb_endpoint *ep, uint len) {
    struct usb_buffer *buffer = usb_current_in_packet_buffer(ep);
    assert(len <= buffer->data_max);
    memset0(buffer->data, len);
    buffer->data_len = len;
    return buffer->data;
}

void safe_reboot(uint32_t addr, uint32_t sp, uint32_t delay_ms) {
    watchdog_reboot(addr, sp, delay_ms);
}
