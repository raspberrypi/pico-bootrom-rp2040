MEMORY {
    ROM(rx) : ORIGIN = 0x00000000, LENGTH = 16K
    SRAM(rwx) : ORIGIN = 0x20000000, LENGTH = 264K
    USBRAM(rw) : ORIGIN = 0x50100400, LENGTH = 3K
}

SECTIONS {
    . = ORIGIN(ROM);
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        KEEP(*(.rodata.keep*))
        *(.rodata*)
        this_is_the_end_my_only_friend_the_end = .;
        . = ALIGN(LENGTH(ROM));
    } >ROM =0x00be

    .data : {
        *(.data*)
    } >USBRAM

    .bss : {
        *(.bss*)
    } >USBRAM

    ASSERT(__irq5_vector == __vectors + 0x40 + 5 * 4, "too much data in middle of vector table")
    ASSERT(SIZEOF(.data) == 0,
        "ERROR: do not use static memory in bootrom! (.data)")

     /* Leave room above the stack for stage 2 load, so that stage 2
       can image SRAM from its beginning */
    _stacktop = ORIGIN(SRAM) + LENGTH(SRAM) - 256;
}
