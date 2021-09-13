add_executable(bit_functions_test
        bit_functions_test.c
        ../bootrom/bit_functions.S)

target_compile_definitions(bit_functions_test PRIVATE
        NEW_BIT_FUNCTIONS=1
        USE_POPCOUNT32
        USE_CLZ32
        USE_CTZ32
        USE_REVERSE32)

target_link_libraries(bit_functions_test PRIVATE pico_stdlib)
pico_add_extra_outputs(bit_functions_test)

add_executable(mem_functions_test
        mem_functions_test.c
        ../bootrom/bootrom_misc.S)

target_link_libraries(mem_functions_test PRIVATE pico_stdlib)
pico_add_extra_outputs(mem_functions_test)

add_executable(tc_rom_float tc_rom_float.c)
add_executable(tc_rom_double tc_rom_double.c)

# don't redirect C floating point code thru the bootrom!
pico_set_float_implementation(tc_rom_float compiler)
pico_set_float_implementation(tc_rom_double compiler)
pico_set_double_implementation(tc_rom_float compiler)
pico_set_double_implementation(tc_rom_double compiler)

target_link_libraries(tc_rom_float PRIVATE pico_stdlib)
target_link_libraries(tc_rom_double PRIVATE pico_stdlib)

pico_add_extra_outputs(tc_rom_float)
pico_add_extra_outputs(tc_rom_double)
