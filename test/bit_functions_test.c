/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "tictoc.h"

extern uint32_t popcount32(uint32_t v);
extern uint32_t reverse32(uint32_t v);
extern uint32_t clz32(uint32_t v);
extern uint32_t ctz32(uint32_t v);

#define ASSERT(x) if (!(x)) { panic("ASSERT: %s l %d: " #x "\n" , __FILE__, __LINE__); }

static bool roughly_ascii(uint x) {
    return x >= 32 && x < 127;
}

static int check_table(uint16_t *table, uint align_mask, uint align_value) {
    static uint16_t entries[256];
    uint32_t (*table_lookup)(uint16_t *, uint32_t) = (uint32_t (*)(uint16_t *, uint32_t))(uint32_t)(*(uint16_t *)0x18);
    int n = 0;
    while (*table && n != count_of(entries)) {
        printf("Checking %c%c %d %d\n", table[0], table[0]>>8u, align_mask, align_value);
        for(int i=0; i<n;i++) {
            ASSERT(entries[i] != table[0]); // disallow duplicate table codes
        }
        ASSERT(roughly_ascii(table[0] & 0xffu));
        ASSERT(roughly_ascii(table[0] >> 8u));
        if ((table[0] & 0xffu) != 'F' || (table[0] >> 8u) != 'Z') // this is byte aligned, so no check
            ASSERT(align_value == (table[1] & align_mask)); // check alignment
        entries[n++] = table[0];
        ASSERT(table_lookup(table, table[0]) == table[1]); // check that looking up the value works correctly
        table += 2;
    }
    ASSERT(n); // table should not be empty
    ASSERT(n != count_of(entries)); // table should terminate relatively quickly
    ASSERT(0 == table_lookup(table, rom_table_code('Z','Z')));
    return 0;
}

// slightly slower than ours, but known to be good
static uint32_t __noinline bit_reverse(uint32_t i) {
i = ((i & 0x55555555u) << 1u) | ((i >> 1u) & 0x55555555u);
i = ((i & 0x33333333u) << 2u) | ((i >> 2u) & 0x33333333u);
i = ((i & 0x0f0f0f0fu) << 4u) | ((i >> 4u) & 0x0f0f0f0fu);
return __bswap32(i);
}

void check_popcount32(uint32_t i, uint32_t o) {
    ASSERT(o == __builtin_popcount(i));
}

void check_reverse32(uint32_t i, uint32_t o) {
    ASSERT(o == bit_reverse(i));
}

void check_clz32(uint32_t i, uint32_t o) {
    if (i && o != __builtin_clz(i)) {
        panic("INPUT %08x EXPECTED %08x GOT %08x\n", i, __builtin_clz(i), o);
    }
}

void check_ctz32(uint32_t i, uint32_t o) {
    if (i && o != __builtin_ctz(i)) {
        panic("INPUT %08x EXPECTED %08x GOT %08x\n", i, __builtin_clz(i), o);
    }
}

static int check_bit_function_internal(uint32_t i, uint32_t (*test_fn)(uint32_t), void (*check_fn)(uint32_t, uint32_t))
{
    uint32_t v = test_fn(i);
    if (check_fn) {
        check_fn(i, v);
    }
    return 0;
}

static int check_bit_function(uint32_t i, uint32_t (*test_fn)(uint32_t), void (*check_fn)(uint32_t, uint32_t), bool bitreverse_input) {
    check_bit_function_internal(i, test_fn, check_fn);
    check_bit_function_internal(i ^ 0xffffffffu, test_fn, check_fn);
    if (bitreverse_input) {
        uint32_t rev = bit_reverse(i);
        check_bit_function_internal(rev, test_fn, check_fn);
        check_bit_function_internal(rev ^ 0xffffffffu, test_fn, check_fn);
    }
    return 0;
}

__attribute__((naked)) uint32_t empty_fn() {
    asm ("bx lr");
}

// note this is not quite exhaustive; it does not test input 0
#define run_exhaustive(name, func, alternate_func) \
{ \
    printf("Exhaustive %s...\n", name); \
    absolute_time_t t = get_absolute_time(); \
    for(uint32_t i=0xffffffff; i>0; i--) { \
        if (func(i) != alternate_func(i)) { \
            panic("%s failed at %08x\n", name, i); \
        } \
        if (!(i&0xfffff)) { \
            int64_t elapsed = time_diff(t, get_absolute_time()); \
            int64_t expected = 4096 * elapsed / (4096 - (i>>20)); \
            int32_t remaining_secs = (expected - elapsed) / 1000000; \
            printf("\r%d %ds    ", (i>>20), remaining_secs); \
        } \
    } \
    printf("\n"); \
}

uint32_t __42(uint32_t x) {
    return 42;
}

int __time_critical_func(main)()
{
    setup_default_uart();

    srand(0xf005ba11);

    uint16_t *func_table = (uint16_t *)(uint32_t)*(uint16_t *) 0x14;
    uint16_t *data_table = (uint16_t *)(uint32_t)*(uint16_t *) 0x16;

    check_table(func_table, 1, 1); // odd values only
    // todo this is not true - we should check they are naturally aligned
    // convention is for these to be word aligned
    check_table(data_table, 3, 0);

    extern uint32_t __clzsi2(uint32_t);
    extern uint32_t __ctzsi2(uint32_t);
    extern uint32_t __popcountsi2(uint32_t);
    static struct {
        const char *name;
        uint32_t (*func)(uint32_t);
        void (*check)(uint32_t, uint32_t);
    } tests[] = {
            { "empty", empty_fn, NULL, },
            { "popcount32", popcount32, check_popcount32, },
            { "reverse32", reverse32, check_reverse32, },
            { "clz32", clz32, check_clz32, },
            { "ctz32", ctz32, check_ctz32, },
            { "__clzsi2", __clzsi2, NULL, },
            { "__clzsi2", __ctzsi2, NULL, },
            { "__popcountsi2", __popcountsi2, NULL, },
    };
    int n_checks = 1 << 16;
    for (int t = 1; t<5;t++) {
        printf("Testing %s...\n", tests[t].name);
        for(int i = 0; i <= n_checks; i++) {
            check_bit_function(i, tests[t].func, tests[t].check, true);
            check_bit_function(-i, tests[t].func, tests[t].check, true);
            check_bit_function((uint) rand(), tests[t].func, tests[t].check, true);
        }
    }

    ASSERT(clz32(0) == 32);
    ASSERT(clz32(-1) == 0);
    ASSERT(ctz32(0) == 32);
    ASSERT(ctz32(-1) == 0);

    tictoc_init();
    // timing
    n_checks = 1 << 13; // about the max we can test before rollover
    uint32_t *rands = (uint32_t *)calloc(n_checks, sizeof(uint32_t));
    for(int i=0;i<n_checks;i++) rands[i] = (uint32_t)rand();
    uint32_t times[count_of(tests)];
    for (int t = 0; t<count_of(tests);t++) {
        printf("Timing %s...\n", tests[t].name);
        uint32_t cycle = cyc();
        for(int i = 0; i <= n_checks; i++) {
            check_bit_function(i, tests[t].func, NULL, false);
            check_bit_function(-i, tests[t].func, NULL, false);
            check_bit_function(rands[t], tests[t].func, NULL, false);
        }
        times[t] = cyc() - cycle;
        printf("   %08x\n", (int)times[t]);
    }
    for (int t = 0; t<count_of(tests);t++) {
        int32_t delta = (times[t] - times[0])>>8u;
        double clocks = delta / (6.0 * n_checks);
        printf("%s %2.2g cycles\n", tests[t].name, clocks);
    }
    free(rands);

#if 0
    run_exhaustive("clz32", clz32, __clzsi2);
    run_exhaustive("ctz32", ctz32, __ctzsi2);
    run_exhaustive("popcount32", popcount32, __popcountsi2);
    run_exhaustive("reverse32", reverse32, bit_reverse);
#endif
    printf("Done\n");
}
