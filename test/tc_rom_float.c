#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"

// NOTE THIS IS JUST A SMOKE TEST OF ALL FLOAT FUNCtiONS, NOT AN EXHAUSTIVE CORRECTNESS TEST

#define ASSERT(x) if (!(x)) { panic("ASSERT: %s l %d: " #x "\n" , __FILE__, __LINE__); }

typedef union {
    float f;
    uint32_t i;
} float_value;

float_value least_neg_norm = { .i = 0x80800000 };
float_value most_neg_denorm = { .i = 0x807fffff };
float_value random_neg_denorm = { .i = 0x80123456 };
float_value least_neg_denorm = { .i = 0x80000001 };
float_value minus_zero = { .i = 0x80000000 };
float_value zero = { .i = 0x00000000 };
float_value least_pos_denorm = { .i = 0x00000001 };
float_value random_pos_denorm = { .i = 0x00123456 };
float_value most_pos_denorm = { .i = 0x007fffff };
float_value least_pos_norm = { .i = 0x00800000 };

typedef int (*i3func)(int, int);

struct mufp_funcs {
    float (*mufp_fadd)(float, float);
    float (*mufp_fsub)(float, float);
    float (*mufp_fmul)(float, float);
    float (*mufp_fdiv)(float, float);
    int (*mufp_fcmp_fast)(float, float);
    void (*mufp_fcmp_fast_flags)(float, float);
    float (*mufp_fsqrt)(float);
    int (*mufp_float2int)(float);
    int (*mufp_float2fix)(float, int);
    uint (*mufp_float2uint)(float);
    uint (*mufp_float2ufix)(float, int);
    float (*mufp_int2float)(int);
    float (*mufp_fix2float)(int, int);
    float (*mufp_uint2float)(uint);
    float (*mufp_ufix2float)(uint, int);
    float (*mufp_fcos)(float);
    float (*mufp_fsin)(float);
    float (*mufp_ftan)(float);
    float (*v3_mufp_fsincos)(float);
    float (*mufp_fexp)(float);
    float (*mufp_fln)(float);

    // these are in rom version 2
    int (*mufp_fcmp)(float, float);
    float (*mufp_fatan2)(float, float);
    float (*mufp_int642float)(int64_t);
    float (*mufp_fix642float)(int64_t, int);
    float (*mufp_uint642float)(int64_t);
    float (*mufp_ufix642float)(int64_t, int);
    int64_t (*mufp_float2int64)(float);
    int64_t (*mufp_float2fix64)(float, int);
    int64_t (*mufp_float2uint64)(float);
    int64_t (*mufp_float2ufix64)(float, int);
    double (*mufp_float2double)(float);

} *mufp_funcs;

float __noinline fadd(float a, float b) {
    return a + b;
}

float __noinline fsub(float a, float b) {
    return a - b;
}

float __noinline fmul(float a, float b) {
    return a * b;
}

float __noinline fdiv(float a, float b) {
    return a / b;
}

float flush(float x) {
    float_value val = { .f = x };
    if (val.i >= zero.i && val.i <= most_pos_denorm.i) x = 0;
    if (val.i >= minus_zero.i && val.i <= most_neg_denorm.i) x = 0;
    return x;
}

int __noinline fcmp_fast(float a, float b) {
    return a < b ? - 1 : (a > b ? 1 : 0);
}

int __noinline fcmp(float a, float b) {
    return fcmp_fast(flush(a), flush(b));
}

float __noinline fsqrt(float a) {
    return sqrtf(a);
}

int __noinline float2int(float a) {
    return (int)a;
}

int64_t __noinline float2int64(float a) {
    return (int64_t)a;
}

int __noinline float2fix(float a, int b) {
    return (int)(a * powf(2.f, b));
}

int64_t __noinline float2fix64(float a, int b) {
    return (int64_t)(a * powf(2.f, b));
}

uint __noinline float2uint(float a) {
    // we do this which seems more useful... a wrapper for casting can choose to call float2int instead and cast that as uint if it wants
    return a < 0 ? 0 : (uint) a;
}

uint64_t __noinline float2uint64(float a) {
    // we do this which seems more useful... a wrapper for casting can choose to call float2int instead and cast that as uint if it wants
    return a < 0 ? 0 : (uint64_t) a;
}


uint __noinline float2ufix(float a, int b) {
    if (a < 0) return 0;
    return (uint)(a * powf(2.f, b));
}

uint64_t __noinline float2ufix64(float a, int b) {
    if (a < 0) return 0;
    return (uint64_t)(a * powf(2.f, b));
}

float int2float(int a) {
    return (float)a;
}

float int642float(int64_t a) {
    return (float)a;
}

float __noinline fix2float(int a, int b) {
    return ((float)a) / powf(2.f, b);
}

float __noinline fix642float(int64_t a, int b) {
    return ((float)a) / powf(2.f, b);
}

float uint2float(uint a) {
    return (float)a;
}

float uint642float(uint64_t a) {
    return (float)a;
}

float ufix2float(uint a, int b) {
    return ((float)a) / powf(2.f, b);
}

float ufix642float(uint64_t a, int b) {
    return ((float)a) / powf(2.f, b);
}

double float2double(float a) {
    return (double)a;
}

float __noinline fcos(float a) {
    return cosf(a);
}

float __noinline fsin(float a) {
    return sinf(a);
}

float __noinline ftan(float a) {
    return tanf(a);
}

float __noinline fatan2(float a, float b) {
    return atan2f(a, b);
}

float __noinline fexp(float a) {
    return expf(a);
}

float __noinline fln(float a) {
    return logf(a);
}

// yuk our ee_printf crashses on infinites and floats
#define safe_for_print(x) (x) != (x) ? -12301.f : ((x) == INFINITY ? -12302.f : ((x) == -INFINITY ? -12303.f : (x)))
// want typeof, but don't want to change build to use new C version at this point
#define check_float(a, b) ({if (!(a == b || fabsf((a)-(b)) < 1e-6f)) printf("%f != %f %s\n", safe_for_print(a), safe_for_print(b), __STRING((b))); ASSERT(a == b || fabsf((a)-(b)) < 1e-6f); })
#define check_int(a, b) ({if ((a)!=(b)) printf("%d != %d %s\n", a, b, __STRING((b))); ASSERT((a) == (b)); })
#define check_uint(a, b) ({if ((a)!=(b)) printf("%u != %u %s\n", a, b, __STRING((b))); ASSERT((a) == (b)); })
#define check_int64(a, b) ({if ((a)!=(b)) printf("%08x%08x != %08x%08x %s\n", (int)(a>>32), (int)a, (int)(b>>32), (int)b, __STRING((b))); ASSERT((a) == (b)); })
#define check_uint64(a, b) ({if ((a)!=(b)) printf("%08x%08x != %08x%08x %s\n", (int)(a>>32), (int)a, (int)(b>>32), (int)b, __STRING((b))); ASSERT((a) == (b)); })

#define check_float_fn1(fn, a) check_float(fn(a), mufp_funcs->mufp_##fn(a))
#define check_float_fn2(fn, a, b) check_float(fn(a, b), mufp_funcs->mufp_##fn(a, b))
#define check_int_fn1(fn, a) check_int(fn(a), mufp_funcs->mufp_##fn(a))
#define check_int64_fn1(fn, a) check_int64(fn(a), mufp_funcs->mufp_##fn(a))
#define check_int_fn2(fn, a, b) check_int(fn(a, b), mufp_funcs->mufp_##fn(a, b))
#define check_int64_fn2(fn, a, b) check_int64(fn(a, b), mufp_funcs->mufp_##fn(a, b))
#define check_uint_fn1(fn, a) check_uint(fn(a), mufp_funcs->mufp_##fn(a))
#define check_uint64_fn1(fn, a) check_uint64(fn(a), mufp_funcs->mufp_##fn(a))
#define check_uint_fn2(fn, a, b) check_uint(fn(a, b), mufp_funcs->mufp_##fn(a, b))
#define check_uint64_fn2(fn, a, b) check_uint64(fn(a, b), mufp_funcs->mufp_##fn(a, b))

int __attribute__((naked)) fcmp_from_fcmp_flags(float a, float b, int (*fmcp_flags)(float, float)) {
    asm(
        "push {r4, lr}\n"
        "mov r4, #1\n"
        "blx r2\n"
        "bge 1f\n"
        "neg r4, r4\n"
        "1:\n"
        "bne 1f\n"
        "sub r4, r4\n"
        "1:\n"
        "mov r0, r4\n"
        "pop {r4, pc}\n"
    );
}

float __attribute__((naked)) call_fsincos(float v, float *cout, float (*func)(float)) {
    asm(
    "push {r1, lr}\n"
    "blx r2\n"
    "pop {r2}\n"
    "str r1, [r2]\n"
    "pop {pc}\n"
    );
}

#define check_fcmp_flags(a,b) check_int(fcmp(a, b), fcmp_from_fcmp_flags(a, b, (void (*)(float, float))mufp_funcs->mufp_fcmp)) // f_cmp is f_cmp_flags now
#define check_fcmp_fast_flags(a,b) check_int(fcmp_fast(a, b), fcmp_from_fcmp_flags(a, b, mufp_funcs->mufp_fcmp_fast_flags))

int main()
{
    setup_default_uart();
 	srand(0xf005ba11);
    mufp_funcs = (struct mufp_funcs *)rom_data_lookup(rom_table_code('S','F'));
    ASSERT(mufp_funcs);

    int rom_version = *(uint8_t*)0x13;
    printf("ROM VERSION %d\n", rom_version);
    if (rom_version > 1) {
        uint8_t *func_count = (uint8_t *)rom_data_lookup(rom_table_code('F','Z'));
        assert(func_count);
        assert(*func_count == sizeof(struct mufp_funcs) / 4);
    }
    int valid_funcs_size = sizeof(struct mufp_funcs);
    if (rom_version == 1) {
        valid_funcs_size -= 11 * 4;
    }
    for(int i=0; i<valid_funcs_size; i+=4) {
        uint32_t fp = *(uint32_t*)(((uint8_t*)mufp_funcs) + i);
        ASSERT(fp);
        ASSERT(fp & 1u); // thumb bit!
        ASSERT(fp < 16 * 1024); // in ROM!
    }

    // very simple sanity tests
    check_float_fn2(fadd, 1.3f, -5.0f);

    check_float_fn2(fsub, 1000.75f, 998.6f);

    check_float_fn2(fmul, 1.75f, 31.4f);

    check_float_fn2(fdiv, 2314.6f, -.37f);
    check_float_fn2(fdiv, 234.6f, -10000.37f);
    check_float_fn2(fdiv, 2314.6f, INFINITY);
    check_float_fn2(fdiv, 2314.6f, -INFINITY);

    if (rom_version > 1) {
        check_int_fn2(fcmp, -3.0f, 7.3f);
        check_int_fn2(fcmp, 3.0f, -7.3f);
        check_int_fn2(fcmp, 3.0f, 3.0f);
        check_int_fn2(fcmp, 3.0f, -INFINITY);
        check_int_fn2(fcmp, 3.0f, INFINITY);

        check_int_fn2(fcmp, least_neg_denorm.f, most_neg_denorm.f);
        check_int_fn2(fcmp, most_neg_denorm.f, least_neg_denorm.f);
        check_int_fn2(fcmp, least_neg_denorm.f, least_neg_denorm.f);
        check_int_fn2(fcmp, least_neg_norm.f, least_neg_denorm.f);
        check_int_fn2(fcmp, least_pos_denorm.f, most_pos_denorm.f);
        check_int_fn2(fcmp, most_pos_denorm.f, least_pos_denorm.f);
        check_int_fn2(fcmp, least_pos_denorm.f, least_pos_denorm.f);
        check_int_fn2(fcmp, least_pos_denorm.f, least_pos_denorm.f);
        check_int_fn2(fcmp, least_pos_norm.f, least_pos_denorm.f);

    }

    check_int_fn2(fcmp_fast, -3.0f, 7.3f);
    check_int_fn2(fcmp_fast, 3.0f, -7.3f);
    check_int_fn2(fcmp_fast, 3.0f, 3.0f);
    check_int_fn2(fcmp_fast, 3.0f, -INFINITY);
    check_int_fn2(fcmp_fast, 3.0f, INFINITY);
    check_int_fn2(fcmp_fast, minus_zero.f, zero.f);

    check_int_fn2(fcmp_fast, least_neg_denorm.f, most_neg_denorm.f);
    check_int_fn2(fcmp_fast, most_neg_denorm.f, least_neg_denorm.f);
    check_int_fn2(fcmp_fast, least_neg_denorm.f, least_neg_denorm.f);
    check_int_fn2(fcmp_fast, least_neg_norm.f, least_neg_denorm.f);
    check_int_fn2(fcmp_fast, least_pos_denorm.f, most_pos_denorm.f);
    check_int_fn2(fcmp_fast, most_pos_denorm.f, least_pos_denorm.f);
    check_int_fn2(fcmp_fast, least_pos_denorm.f, least_pos_denorm.f);
    check_int_fn2(fcmp_fast, least_pos_denorm.f, least_pos_denorm.f);
    check_int_fn2(fcmp_fast, least_pos_norm.f, least_pos_denorm.f);

    if (rom_version > 1) {
        check_fcmp_flags(-3.0f, 7.3f);
        check_fcmp_flags(3.0f, -7.3f);
        check_fcmp_flags(3.0f, 3.0f);
        check_fcmp_flags(3.0f, -INFINITY);
        check_fcmp_flags(3.0f, INFINITY);

        check_fcmp_flags(least_neg_denorm.f, most_neg_denorm.f);
        check_fcmp_flags(most_neg_denorm.f, least_neg_denorm.f);
        check_fcmp_flags(least_neg_denorm.f, least_neg_denorm.f);
        check_fcmp_flags(least_neg_norm.f, least_neg_denorm.f);
        check_fcmp_flags(least_pos_denorm.f, most_pos_denorm.f);
        check_fcmp_flags(most_pos_denorm.f, least_pos_denorm.f);
        check_fcmp_flags(least_pos_denorm.f, least_pos_denorm.f);
        check_fcmp_flags(least_pos_denorm.f, least_pos_denorm.f);
        check_fcmp_flags(least_pos_norm.f, least_pos_denorm.f);
    }

    check_fcmp_fast_flags(-3.0f, 7.3f);
    check_fcmp_fast_flags(3.0f, -7.3f);
    check_fcmp_fast_flags(3.0f, 3.0f);
    check_fcmp_fast_flags(3.0f, -INFINITY);
    check_fcmp_fast_flags(3.0f, INFINITY);

    check_fcmp_fast_flags(least_neg_denorm.f, most_neg_denorm.f);
    check_fcmp_fast_flags(most_neg_denorm.f, least_neg_denorm.f);
    check_fcmp_fast_flags(least_neg_denorm.f, least_neg_denorm.f);
    check_fcmp_fast_flags(least_neg_norm.f, least_neg_denorm.f);
    check_fcmp_fast_flags(least_pos_denorm.f, most_pos_denorm.f);
    check_fcmp_fast_flags(most_pos_denorm.f, least_pos_denorm.f);
    check_fcmp_fast_flags(least_pos_denorm.f, least_pos_denorm.f);
    check_fcmp_fast_flags(least_pos_denorm.f, least_pos_denorm.f);
    check_fcmp_fast_flags(least_pos_norm.f, least_pos_denorm.f);

    check_float_fn1(fsqrt, 3.0f);

    // we are returning INFINITE not NAN as we don't support NANs
//    check_float_fn1(fsqrt, -3.0f);
    if (rom_version == 1)
    {
        // is buggy in rom version 1
        ASSERT(INFINITY == mufp_funcs->mufp_fsqrt(-3.0f));
    } else {
        ASSERT(-INFINITY == mufp_funcs->mufp_fsqrt(-3.0f));
    }

    check_int_fn1(float2int, 3.f);
    check_int_fn1(float2int, 123456000000.f);
    check_int_fn1(float2int, -3.f);
    check_int_fn1(float2int, -123456000000.f);
    check_int_fn1(float2int, INFINITY);
    check_int_fn1(float2int, -INFINITY);

    check_int_fn2(float2fix, 3.f, 3);
    check_int_fn2(float2fix, 31.f, -3);
    check_int_fn2(float2fix, -3.f, 3);
    // todo JURY IS OUT ON THIS ONE
    //check_int_fn2(float2fix, -31.f, -3);

    check_uint_fn1(float2uint, 3.f);
    check_uint_fn1(float2uint, 123456000000.f);
    check_uint_fn1(float2uint, -3.f);
    check_uint_fn1(float2uint, -123456000000.f);

    check_uint_fn2(float2ufix, 3.f, 3);
    check_uint_fn2(float2ufix, 3.f, -3);

    check_float_fn1(int2float, 3);
    check_float_fn1(int2float, INT32_MAX);
    check_float_fn1(int2float, INT32_MIN);
    check_float_fn1(int2float, -3);

    check_float_fn2(fix2float, 3, 3);
    check_float_fn2(fix2float, 3, -3);
    check_float_fn2(fix2float, -3, 3);
    check_float_fn2(fix2float, -3, -3);

    check_float_fn1(uint2float, 3);
    check_float_fn1(uint2float, UINT32_MAX);

//    float (*mufp_ufix2float)(uint, int);

    check_float_fn1(fcos, 0.0f);
    check_float_fn1(fcos, 2.7f);
    check_float_fn1(fcos, -32.7f);

    check_float_fn1(fsin, 0.0f);
    check_float_fn1(fsin, 2.7f);
    check_float_fn1(fsin, -32.7f);

    check_float_fn1(ftan, 0.0f);
    check_float_fn1(ftan, 2.7f);
    check_float_fn1(ftan, -32.7f);

    if (rom_version > 1) {
        // broken on rom v 1
        // todo check broken range
        check_float_fn2(fatan2, 3.0f, 4.0f);
        check_float_fn2(fatan2, -3.0f, 4.0f);
        check_float_fn2(fatan2, 4.0f, -.31f);
        check_float_fn2(fatan2, -3.0f, -.17f);
    }

    check_float_fn1(fexp, 0.0f);
    check_float_fn1(fexp, 2.7f);
    check_float_fn1(fexp, -32.7f);

    check_float_fn1(fln, 0.3f);
    check_float_fn1(fln, 1.0f);
    check_float_fn1(fln, 2.7f);
    // we are returning -INFINITE as we don't support NANs
//    check_float_fn1(fln, -32.7f);
    ASSERT(-INFINITY == mufp_funcs->mufp_fln(-32.7f));

    if (rom_version > 1) {
        check_int64_fn1(float2int64, 3.f);
        check_int64_fn1(float2int64, 123456000000.f);
        check_int64_fn1(float2int64, 12345678912345.f);
        check_int64_fn1(float2int64, -3.f);
        check_int64_fn1(float2int64, -123456000000.f);
        check_int64_fn1(float2int64, -12345678912345.f);

        // seems like gcc is wrong on this one
//        check_int64_fn1(float2int64, INFINITY);
        // so
        ASSERT(INT64_MAX == mufp_funcs->mufp_float2int64(INFINITY));

        // seems like gcc is wrong on this one
//        check_int64_fn1(float2int64, -INFINITY);
        // so
        ASSERT( INT64_MIN == mufp_funcs->mufp_float2int64(-INFINITY));


        check_int64_fn2(float2fix64, 3.f, 3);
        check_int64_fn2(float2fix64, 31.f, -3);
        check_int64_fn2(float2fix64, -3.f, 3);
        // todo JURY IS OUT ON THIS ONE
        //check_int64_fn2(float2fix64, -31.f, -3);

        check_uint64_fn1(float2uint64, 3.f);
        check_uint64_fn1(float2uint64, 123456000000.f);
        check_uint64_fn1(float2uint64, 12345678912345.f);
        check_uint64_fn1(float2uint64, -3.f);
        check_uint64_fn1(float2uint64, -123456000000.f);
        check_uint64_fn1(float2uint64, -12345678912345.f);

        check_uint64_fn2(float2ufix64, 3.f, 3);
        check_uint64_fn2(float2ufix64, 3.f, 43);
        check_uint64_fn2(float2ufix64, 3.f, -3);
        check_uint64_fn2(float2ufix64, 3.f, -43);

#define LARGE 0x1234567800000000ll
        check_float_fn1(int642float, 3);
        check_float_fn1(int642float, LARGE);
        check_float_fn1(int642float, INT32_MAX);
        check_float_fn1(int642float, INT64_MAX);
        check_float_fn1(int642float, INT32_MIN);
        check_float_fn1(int642float, INT64_MIN);
        check_float_fn1(int642float, -3);
        check_float_fn1(int642float, -LARGE);

        check_float_fn2(fix642float, 3, 3);
        check_float_fn2(fix642float, 3, -3);
        check_float_fn2(fix642float, -3, 3);
        check_float_fn2(fix642float, -3, -3);

        check_float_fn2(fix642float, LARGE, 3);
        check_float_fn2(fix642float, LARGE, -3);
        check_float_fn2(fix642float, -LARGE, 3);
        check_float_fn2(fix642float, -LARGE, -3);

        check_float_fn1(uint642float, 3);
        check_float_fn1(uint642float, UINT32_MAX);
        check_float_fn1(uint642float, UINT64_MAX);
        check_float_fn1(uint642float, LARGE);

        check_float_fn1(float2double, 3.0f);
        check_float_fn1(float2double, 3.0e12f);
        check_float_fn1(float2double, -3.0e12f);
        check_float_fn1(float2double, -3.131592754123214125f);
        check_float_fn1(float2double, 3.131592754123214125f);
        check_float_fn1(float2double, -INFINITY);
        check_float_fn1(float2double, INFINITY);
        check_float_fn1(float2double, most_pos_denorm.f);
        check_float_fn1(float2double, least_pos_denorm.f);
        check_float_fn1(float2double, least_pos_norm.f);
        check_float_fn1(float2double, least_neg_denorm.f);
        check_float_fn1(float2double, least_neg_norm.f);
        check_float_fn1(float2double, most_neg_denorm.f);
        check_float_fn1(float2double, 0.f);
        check_float_fn1(float2double, -0.f);
    }

    if (rom_version >= 3) {
        for(float a = -0.3f; a<7.f; a += 0.137) {
            float s = mufp_funcs->mufp_fsin(a);
            float c = mufp_funcs->mufp_fcos(a);
            float co=0;
            float so = call_fsincos(a, &co, mufp_funcs->v3_mufp_fsincos);
            ASSERT(so == s && co == c);
        }
    }
    printf("FLOAT OK\n");
	return 0;
}
