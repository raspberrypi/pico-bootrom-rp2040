cmake_minimum_required(VERSION 3.12)
#set(CMAKE_C_STANDARD 11)

# Pull in PICO SDK (must be before project)
include(pico_sdk/pico_sdk_init.cmake)

project(pico_bootrom)

if (NOT INCLUDE_TESTS)
    set(PICO_BARE_METAL 1)
else()
    # SDK versions conflict with code to be tested
    set(SKIP_PICO_BIT_OPS 1)
endif()

# Add pico targets to the build
pico_sdk_init()

# We want to generate from WELCOME.HTM and
include(ExternalProject)
ExternalProject_Add(generator
        PREFIX generator
        SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/generator
        BINARY_DIR ${CMAKE_BINARY_DIR}/generator
        DOWNLOAD_COMMAND ""
        INSTALL_COMMAND ""
        )

add_executable(generate IMPORTED)
# todo is there a better way - this breaks on windows I presume
set_property(TARGET generate PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/generator/generate)
add_dependencies(generate generator)

set(GIT_INFO_H ${CMAKE_CURRENT_BINARY_DIR}/git_info.h)
set(GENERATED_H ${CMAKE_CURRENT_BINARY_DIR}/generated.h)

# order matches original makefile
add_executable(bootrom
        bootrom/bootrom_rt0.S
        bootrom/bit_functions.S
        bootrom/bootrom_main.c
        bootrom/bootrom_misc.S
        bootrom/program_flash_generic.c
        bootrom/usb_boot_device.c
        bootrom/virtual_disk.c
        bootrom/async_task.c
        bootrom/mufplib.S
        bootrom/mufplib-double.S
        usb_device_tiny/runtime.c
        usb_device_tiny/usb_device.c
        usb_device_tiny/usb_msc.c
        usb_device_tiny/usb_stream_helper.c
        )

target_include_directories(bootrom PRIVATE bootrom usb_device_tiny ${CMAKE_CURRENT_BINARY_DIR})

add_custom_target(update_git_info DEPENDS ${GIT_INFO_H})

add_custom_command(OUTPUT ${GIT_INFO_H}
        COMMENT "Generating ${GIT_INFO_H}"
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/.git/index
        COMMAND ${CMAKE_COMMAND} -E echo_append " #define GIT_REV 0x" > ${GIT_INFO_H}
        COMMAND git rev-parse --short=8 HEAD >> ${GIT_INFO_H}
        )

add_custom_target(generate_header DEPENDS ${GENERATED_H})
add_custom_command(OUTPUT ${GENERATED_H}
        COMMENT "Generating ${GENERATED_H}"
        DEPENDS bootrom/info_uf2.txt bootrom/welcome.html usb_device_tiny/scsi_ir.h
        COMMAND generate ${CMAKE_CURRENT_LIST_DIR}/bootrom >${GENERATED_H}
        )

add_dependencies(bootrom update_git_info)
add_dependencies(bootrom generate_header)

# we need these for both compile and link due to flto
function(target_cl_options option)
    target_compile_options(bootrom PRIVATE ${option})
    target_link_options(bootrom PRIVATE ${option})
endfunction()

target_cl_options("-fno-jump-tables")
target_cl_options("-g")
target_cl_options("-Os")
target_cl_options("-ffunction-sections")
target_cl_options("-fdata-sections")
target_cl_options("-flto")
target_cl_options("-nostdlib")
target_cl_options("-nodefaultlibs")

target_link_options(bootrom PRIVATE "LINKER:--build-id=none")
target_link_options(bootrom PRIVATE "LINKER:--undefined=isr_irq5")
target_link_options(bootrom PRIVATE "LINKER:--gc-sections")
#target_link_options(bootrom PRIVATE "--save-temps")
target_link_options(bootrom PRIVATE "--specs=nosys.specs")
target_link_options(bootrom PRIVATE "-nostartfiles")

#target_compile_options(bootrom PRIVATE --save-temps)
target_cl_options("-Wall")
target_cl_options("-Wextra")
# Have seen some awful code without this:
target_cl_options("--param")
target_cl_options("max-completely-peel-times=4")

target_compile_definitions(bootrom PRIVATE
        NDEBUG
        USE_PICOBOOT
        USB_MAX_ENDPOINTS=5
        COMPRESS_TEXT
        GENERAL_SIZE_HACKS
        BOOTROM_ONLY_SIZE_HACKS
        USE_16BIT_ROM_FUNCS
        USE_BOOTROM_GPIO

        # for
        USE_HW_DIV

        USE_POPCOUNT32
        USE_CLZ32
        USE_CTZ32
        USE_REVERSE32
)

target_link_options(bootrom PRIVATE "LINKER:--script=${CMAKE_CURRENT_LIST_DIR}/bootrom/bootrom.ld")
set_target_properties(bootrom PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/bootrom/bootrom.ld)
target_link_libraries(bootrom PRIVATE
        hardware_resets
        hardware_regs
        hardware_structs
        pico_platform
        hardware_sync
        boot_uf2_headers
        boot_picoboot_headers
        )

function(pico_add_h32_output TARGET)
    find_package (Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/bin2hex -32 ${TARGET}.bin ${TARGET}.h32)
endfunction()

pico_add_bin_output(bootrom)
pico_add_dis_output(bootrom)
pico_add_map_output(bootrom)
pico_add_hex_output(bootrom)
pico_add_h32_output(bootrom)

if (INCLUDE_TESTS)
    add_subdirectory(test)
endif()


