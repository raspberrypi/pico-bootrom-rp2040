/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _USB_DEVICE_H
#define _USB_DEVICE_H

#include "usb_common.h"
#include "runtime.h"

// ------------------------------------------------------------------------------------
// In order to save space in bootrom, remove unused features, or simplify some
// abstractions that are not required, whilst leaving the code intent intact
// ------------------------------------------------------------------------------------

// GENERAL_SIZE_HACKS (set in build) size hacks (e.g. asm) that are valid in bootrom and usb_device_test
// BOOTROM_ONLY_SIZE_HACK (set in build) size hacks that require this to be actual bootrom (e.g. 16 bit pointers)

#define USB_ASSUME_ZERO_INIT

#ifndef USB_BOOT_WITH_SUBSET_OF_INTERFACES
#define USB_ASSUME_ENDPOINTS_ARRAY_FULL
#endif

// no custom per device setup packet handler
#define USB_NO_DEVICE_SETUP_HANDLER

// since our device has on_configure, require it so save a null test
#define USB_MUST_HAVE_DEVICE_ON_CONFIGURE

// since all our non 0 endpoints are bulk, require that to allow compile time constants
#define USB_BULK_ONLY_EP1_THRU_16

// our interfaces are zero based number in the order they appear on the device - require that
#define USB_ZERO_BASED_INTERFACES

// no custom per endpoint setup packet handlers
#define USB_NO_ENDPOINT_SETUP_HANDLER

// do on_init method for transfer
#define USB_NO_TRANSFER_ON_INIT

// do on_cancel method for transfer
#define USB_NO_TRANSFER_ON_CANCEL

// don't store the endpoints on the interface, and as a result
// just do some endpoint level init during the endpoint initializers
#define USB_NO_INTERFACE_ENDPOINTS_MEMBER

#define USB_NO_INTERFACE_ALTERNATES

#ifndef USB_ISOCHRONOUS_BUFFER_STRIDE_TYPE
#define USB_ISOCHRONOUS_BUFFER_STRIDE_TYPE 0
#endif

static_assert(USB_ISOCHRONOUS_BUFFER_STRIDE_TYPE >= 0 && USB_ISOCHRONOUS_BUFFER_STRIDE_TYPE < 4, "");

// don't zero out most structures (since we do so globablly for BSS)
#define USB_SKIP_COMMON_INIT

// only 16 bytes saved to not set a sense code
//#define USB_SILENT_FAIL_ON_EXCLUSIVE

#ifndef NDEBUG
#define __removed_for_space(x) x
#define __comma_removed_for_space(x) ,x
#else
#define __removed_for_space(x)
#define __comma_removed_for_space(x)
#endif
struct usb_transfer;
struct usb_endpoint;

typedef void (*usb_transfer_func)(struct usb_endpoint *ep);
typedef void (*usb_transfer_completed_func)(struct usb_endpoint *ep, struct usb_transfer *transfer);

struct usb_buffer {
    uint8_t *data;
    uint8_t data_len;
    uint8_t data_max;
    // then...
    bool valid; // aka user owned
};

#include "usb_device_private.h"

struct usb_transfer_type {
    // for IN transfers this is called to setup new packet buffers
    // for OUT transfers this is called with packet data
    //
    // In any case usb_packet_done must be called if this function has handled the buffer
    __rom_function_type(usb_transfer_func) on_packet;
#ifndef USB_NO_TRANSFER_ON_INIT
    usb_transfer_func on_init;
#endif
#ifndef USB_NO_TRANSFER_ON_CANCEL
    usb_transfer_func on_cancel;
#endif
    uint8_t initial_packet_count;
};

struct usb_interface *usb_interface_init(struct usb_interface *interface, const struct usb_interface_descriptor *desc,
                                         struct usb_endpoint *const *endpoints, uint endpoint_count,
                                         bool double_buffered);
struct usb_device *usb_device_init(const struct usb_device_descriptor *desc,
                                   const struct usb_configuration_descriptor *config_desc,
                                   struct usb_interface *const *interfaces, uint interface_count,
                                   const char *(*get_descriptor_string)(uint index));

void usb_device_start();
void usb_device_stop();

// explicit stall
void usb_halt_endpoint_on_condition(struct usb_endpoint *ep);
void usb_halt_endpoint(struct usb_endpoint *endpoint);
void usb_clear_halt_condition(struct usb_endpoint *ep);
static inline bool usb_is_endpoint_stalled(struct usb_endpoint *endpoint);
void usb_set_default_transfer(struct usb_endpoint *ep, struct usb_transfer *transfer);
void usb_reset_transfer(struct usb_transfer *transfer, const struct usb_transfer_type *type,
                        usb_transfer_completed_func on_complete);
void usb_start_transfer(struct usb_endpoint *ep, struct usb_transfer *transfer);
void usb_reset_and_start_transfer(struct usb_endpoint *ep, struct usb_transfer *transfer,
                                  const struct usb_transfer_type *type, usb_transfer_completed_func on_complete);
void usb_chain_transfer(struct usb_endpoint *ep, struct usb_transfer *transfer);
void usb_grow_transfer(struct usb_transfer *transfer, uint packet_count);
void usb_start_default_transfer_if_not_already_running_or_halted(struct usb_endpoint *ep);

typedef void (*usb_transfer_func)(struct usb_endpoint *ep);

struct usb_buffer *usb_current_in_packet_buffer(struct usb_endpoint *ep);
struct usb_buffer *usb_current_out_packet_buffer(struct usb_endpoint *ep);
uint8_t *usb_get_single_packet_response_buffer(struct usb_endpoint *ep, uint len);

// call during (or asynchronously after) on_packet to mark the packet as done
void usb_packet_done(struct usb_endpoint *ep);

extern const struct usb_transfer_type usb_current_packet_only_transfer_type;

static inline struct usb_endpoint *usb_get_control_in_endpoint();
static inline struct usb_endpoint *usb_get_control_out_endpoint();

void usb_start_empty_control_in_transfer(usb_transfer_completed_func on_complete);
void usb_start_empty_control_in_transfer_null_completion();
void usb_start_tiny_control_in_transfer(uint32_t data, uint len);
void usb_start_single_buffer_control_in_transfer();
void usb_start_control_out_transfer(const struct usb_transfer_type *type);
void usb_start_empty_transfer(struct usb_endpoint *endpoint, struct usb_transfer *transfer,
                              usb_transfer_completed_func on_complete);

void usb_soft_reset_endpoint(struct usb_endpoint *ep);
void usb_hard_reset_endpoint(struct usb_endpoint *ep);

#ifdef ENABLE_DEBUG_TRACE
void usb_dump_trace(void);
void usb_reset_trace(void);
#else

static inline void usb_dump_trace() {}

static inline void usb_reset_trace() {}

#endif

#endif