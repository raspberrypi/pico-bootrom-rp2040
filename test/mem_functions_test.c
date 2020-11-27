/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "tictoc.h"

#define ASSERT(x) if (!(x)) { panic("ASSERT: %s l %d: " #x "\n" , __FILE__, __LINE__); }

typedef void *(*memcpy_func)(void *a, void *b, uint len);
memcpy_func memcpy_general, memcpy_44;

__attribute__((naked)) void *memcpy_slow(void *a, void *b, uint c) {
    asm (
    "cmp r2, #0\n"
    "beq 1f\n"
    "mov ip, r0\n"
    "2:\n"
    "sub r2, #1\n"
    "ldrb r3, [r1, r2]\n"
    "strb r3, [r0, r2]\n"
    "bne 2b\n"
    "mov r0, ip\n"
    "1:\n"
    "bx lr\n"
    );
}

__attribute__((naked)) void *memset_slow(void *a, int v, uint c) {
    asm(
    "cmp r2, #0\n"
    "beq 1f\n"
    "mov ip, r0\n"
    "2: \n"
    "sub r2, #1\n"
    "strb r1, [r0, r2] \n"
    "bne 2b\n"
    "mov r0, ip\n"
    "1:\n"
    "bx lr \n"
    );
}


#define TIC t1=cyc();
#define TOC(x) t1=cyc()-t1; x=t1>>8u; x-=3; // timing overhead

void * __noinline tictoc_memcpy_slow(uint8_t *a, uint8_t *b, uint c, uint32_t *t) {
uint t1 = 0;
TIC;
void *rc = memcpy_slow(a, b, c);
TOC(*t);
return rc;
}

void * __noinline tictoc_memcpy_general(uint8_t *a, uint8_t *b, uint c, uint32_t *t) {
uint t1 = 0;
TIC;
void *rc = memcpy_general(a, b, c);
TOC(*t);
return rc;
}

void * __noinline tictoc_memcpy(uint8_t *a, uint8_t *b, uint c, uint32_t *t) {
uint t1 = 0;
TIC;
void *rc = memcpy(a, b, c);
TOC(*t);
return rc;
}

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t s[4] = {0x5a5a5a5a5a5a5a5au, 0xf005ba11deadbeefu, 0xb007c0d3caf3bab3u, 0x0123456789abcdefu};

uint64_t xrand(void) {
    const uint64_t result = rotl(s[0] + s[3], 23) + s[0];

    const uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];

    s[2] ^= t;

    s[3] = rotl(s[3], 45);

    return result;
}

void *__memcpy(void *a, void *b, uint len);
void *__memcpy_44(void *a, void *b, uint len);

static int check_memcpy() {
    printf("------------------- MEMCPY ---------------------\n");
    int ret = 0;
    memcpy_general = __memcpy;
    memcpy_44 = __memcpy_44;
    if (!memcpy_general || !memcpy_44) {
        printf("Doh\n");
        ret = 1;
    }

    static uint8_t source[1024];
    static uint8_t desta[1024];
    static uint8_t destb[1024];
    static uint8_t destc[1024];
    for(int len = 0 ; !ret && len < 1020; len++) {
        for(int i = 0; i<len + 4;i++) {
            source[i] = xrand();
        }
        for(int src_off = 0; !ret && src_off < 4; src_off++) {
            for(int dst_off = 0; !ret && dst_off < 4; dst_off++) {
                memcpy_func mc;
                for(int v = 0; v < 3; v++) {
                    switch (v) {
                        case 0:
                            mc = memcpy_slow;
                            break;
                        case 1:
                            mc = memcpy_general;
                            break;
                        default:
                            if (src_off || dst_off) {
                                continue;
                            }
                            mc = memcpy_44;
                    }
                    memset(desta, 0, len);
                    memset(destb, 0, len);
                    void *a = memcpy(desta + dst_off, source + src_off, len);
//                    if (v == 2 && dst_off == 0 && len == 4) {
//                        __breakpoint();
//                    }
//
                    void *b = mc(destb + dst_off, source + src_off, len);
                    b += desta - destb;
                    uint x = memcmp(desta, destb, 1024);
                    if (a != b || x) {
                        printf("Failed v %d +%d->+%d len = %d (%p/%p/%d)\n", v, src_off, dst_off, len, a, b, x);
                        mc(destb + dst_off, source + src_off, len); // for debugging
                        ret = 1;
                    }
                }
            }
        }
    }
    tictoc_init();

    for(int len = 0 ; !ret && len < 128; len++)
    {
        for(int src_off = 0; !ret && src_off < 4; src_off++)
        {
            for(int dst_off = 0; !ret && dst_off < 4; dst_off++)
            {
                uint32_t ta, tb, tc;
                void *a = tictoc_memcpy_slow(desta + dst_off, source + src_off, len, &ta);
                void *b = tictoc_memcpy_general(destb + dst_off, source + src_off, len, &tb);
                __unused void *c = tictoc_memcpy(destc + dst_off, source + src_off, len, &tc);
                b += desta - destb;
                uint x = memcmp(desta, destb, 1024);
                if (a != b || x)
                {
                    printf("Failed +%d->+%d len = %d (%p/%p/%d)\n", src_off, dst_off, len, a, b, x);
                    ret = 1;
                } else {
                    printf("+%d->+%d len = %d\t%d\t%d\t%d\n", src_off, dst_off, len, (int)ta, (int)tb, (int)tc);
                }
            }
        }
    }
    return ret;
}

typedef void *(*memset_func)(void *a, int c, uint len);
memset_func memset_general, memset_4;

void *__memset(void *a, int c, uint len);
void *__memset_4(void *a, int c, uint len);

void * __noinline tictoc_memset_slow(uint8_t *a, int b, uint c, uint32_t *t) {
uint t1 = 0;
TIC;
void *rc = memset_slow(a, b, c);
TOC(*t);
return rc;
}

void * __noinline tictoc_memset_general(uint8_t *a, int b, uint c, uint32_t *t) {
uint t1 = 0;
TIC;
void *rc = memset_general(a, b, c);
TOC(*t);
return rc;
}

void * __noinline tictoc_memset(uint8_t *a, int b, uint c, uint32_t *t) {
uint t1 = 0;
TIC;
void *rc = memset(a, b, c);
TOC(*t);
return rc;
}

static int check_memset() {
    printf("------------------- MEMSET ---------------------\n");
    int ret = 0;
    memset_general = __memset;
    memset_4 = __memset_4;
    if (!memset_general || !memset_4) {
        printf("Doh\n");
        ret = 1;
    }

    static uint8_t desta[1024];
    static uint8_t destb[1024];
    static uint8_t destc[1024];
    for(int len = 0 ; !ret && len < 1020; len++) {
        for(int dst_off = 0; !ret && dst_off < 4; dst_off++) {
            memset_func ms;
            for(int v = 0; v < 3; v++) {
                switch (v) {
                    case 0:
                        ms = memset_slow;
                        break;
                    case 1:
                        ms = memset_general;
                        break;
                    default:
                        if (dst_off) {
                            continue;
                        }
                        ms = memset_4;
                }
                memset(desta, 0, len);
                memset(destb, 0, len);
                int c = (1u + xrand()) & 0xfeu;
                void *a = memset(desta + dst_off, c, len);
                void *b = ms(destb + dst_off, c, len);
                b += desta - destb;
                uint x = memcmp(desta, destb, 1024);
                if (a != b || x) {
                    printf("Failed v %d +%d len = %d (%p/%p/%d)\n", v, dst_off, len, a, b, x);
                    ms(destb + dst_off, c, len); // for debugging
                    ret = 1;
                }
            }
        }
    }
    *(volatile unsigned int *)0xe000e010=5; // enable SYSTICK at core clock

    for(int len = 0 ; !ret && len < 128; len++)
    {
        for(int dst_off = 0; !ret && dst_off < 4; dst_off++)
        {
            int c = (1 + xrand()) & 0xfeu;
            uint32_t ta, tb, tc;
            void *a = tictoc_memset_slow(desta + dst_off, c, len, &ta);
            void *b = tictoc_memset_general(destb + dst_off, c, len, &tb);
            __unused void *_c = tictoc_memset(destc + dst_off, c, len, &tc);
            b += desta - destb;
            uint x = memcmp(desta, destb, 1024);
            if (a != b || x)
            {
                printf("Failed +%d len = %d (%p/%p/%d)\n", dst_off, len, a, b, x);
                ret = 1;
            } else {
                printf("+%d len = %d\t%d\t%d\t%d\n", dst_off, len, (int)ta, (int)tb, (int)tc);
            }
        }
    }
    return ret;
}

int main() {
    setup_default_uart();
    ASSERT(!check_memcpy());
    ASSERT(!check_memset());
    printf("OK\n");
    return 0;
}