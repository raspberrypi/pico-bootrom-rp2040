#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"

// NOTE THIS IS JUST A SMOKE TEST OF ALL FLOAT FUNCtiONS, NOT AN EXHAUSTIVE CORRECTNESS TEST

#define ASSERT(x) if (!(x)) { panic("ASSERT: %s l %d: " #x "\n" , __FILE__, __LINE__); }

typedef union {
    double d;
    uint64_t l;
} double_value;

double_value least_neg_norm = { .l =  0x8010000000000000ll };
double_value most_neg_denorm = { .l = 0x800fffffffffffffll };
double_value random_neg_denorm = { .l = 0x800123456789abcdll };
double_value least_neg_denorm = { .l = 0x8000000000000001ll };
double_value least_neg_denorm2 = { .l = 0x8000000000000002ll };
double_value minus_zero = { .l = 0x8000000000000000ll };
double_value zero = { .l = 0x0000000000000000ll };
double_value least_pos_denorm = { .l = 0x0000000000000001ll };
double_value least_pos_denorm2 = { .l = 0x0000000000000002ll };
double_value random_pos_denorm = { .l = 0x000123456789abcdll };
double_value most_pos_denorm = { .l = 0x000fffffffffffffll };
double_value least_pos_norm = { .l = 0x0010000000000000ll };

typedef int (*i3func)(int, int);

struct mufp_funcs {
    double (*mufp_dadd)(double, double);
    double (*mufp_dsub)(double, double);
    double (*mufp_dmul)(double, double);
    double (*mufp_ddiv)(double, double);
    int (*mufp_dcmp_fast)(double, double);
    void (*mufp_dcmp_fast_flags)(double, double);
    double (*mufp_dsqrt)(double);
    int (*mufp_double2int)(double);
    int (*mufp_double2fix)(double, int);
    uint (*mufp_double2uint)(double);
    uint (*mufp_double2ufix)(double, int);
    double (*mufp_int2double)(int);
    double (*mufp_fix2double)(int, int);
    double (*mufp_uint2double)(uint);
    double (*mufp_ufix2double)(uint, int);
    double (*mufp_dcos)(double);
    double (*mufp_dsin)(double);
    double (*mufp_dtan)(double);
    double (*v3_mufp_dsincos)(double);
    double (*mufp_dexp)(double);
    double (*mufp_dln)(double);

    // these are in rom version 2
    int (*mufp_dcmp)(double, double);
    double (*mufp_datan2)(double, double);
    double (*mufp_int642double)(int64_t);
    double (*mufp_fix642double)(int64_t, int);
    double (*mufp_uint642double)(int64_t);
    double (*mufp_ufix642double)(int64_t, int);
    int64_t (*mufp_double2int64)(double);
    int64_t (*mufp_double2fix64)(double, int);
    int64_t (*mufp_double2uint64)(double);
    int64_t (*mufp_double2ufix64)(double, int);

    float (*mufp_double2float)(double);
} *mufp_funcs;

double __noinline dadd(double a, double b) {
    return a + b;
}

double __noinline dsub(double a, double b) {
    return a - b;
}

double __noinline dmul(double a, double b) {
    return a * b;
}

double __noinline ddiv(double a, double b) {
    return a / b;
}

int __noinline dcmp_fast(double a, double b) {
    return a < b ? - 1 : (a > b ? 1 : 0);
}

static double flush(double x) {
    double_value val = { .d = x };
    if (val.l >= zero.l && val.l <= most_pos_denorm.l) x = 0;
    if (val.l >= minus_zero.l && val.l <= most_neg_denorm.l) x = 0;
    return x;
}

int __noinline dcmp(double a, double b) {
    return dcmp_fast(flush(a), flush(b));
}

double __noinline dsqrt(double a) {
    return sqrtf(a);
}

int __noinline double2int(double a) {
    return (int)a;
}

int64_t __noinline double2int64(double a) {
    return (int64_t)a;
}

int __noinline double2fix(double a, int b) {
    return (int)(a * pow(2.0, b));
}

int64_t __noinline double2fix64(double a, int b) {
    return (int64_t)(a * powf(2.0, b));
}

uint __noinline double2uint(double a) {
    // we do this which seems more useful... a wrapper for casting can choose to call double2int instead and cast that as uint if it wants
    return a < 0 ? 0 : (uint) a;
}

uint64_t __noinline double2uint64(double a) {
    // we do this which seems more useful... a wrapper for casting can choose to call double2int instead and cast that as uint if it wants
    return a < 0 ? 0 : (uint64_t) a;
}

uint __noinline double2ufix(double a, int b) {
    if (a < 0) return 0;
    return (uint)(a * pow(2.0, b));
}

uint64_t __noinline double2ufix64(double a, int b) {
    if (a < 0) return 0;
    return (uint64_t)(a * powf(2.0, b));
}

double int2double(int a) {
    return (double)a;
}

double int642double(int64_t a) {
    return (double)a;
}

double __noinline fix2double(int a, int b) {
    return ((double)a) / pow(2.0, b);
}

double __noinline fix642double(int64_t a, int b) {
    return ((double)a) / powf(2.0, b);
}

double uint2double(uint a) {
    return (double)a;
}

double uint642double(uint64_t a) {
    return (double)a;
}

double ufix2double(uint a, int b) {
    return ((double)a) / pow(2.0, b);
}

double ufix642double(uint64_t a, int b) {
    return ((double)a) / powf(2.0, b);
}

float double2float(double a) {
    return (float)a;
}

double __noinline dcos(double a) {
    return cos(a);
}

double __noinline dsin(double a) {
    return sin(a);
}

double __noinline dtan(double a) {
    return tan(a);
}

double __noinline datan2(double a, double b) {
    return atan2(a, b);
}

double __noinline dexp(double a) {
    return exp(a);
}

double __noinline dln(double a) {
    return log(a);
}

// yuk our ee_printf crashses on infinites and doubles
#define safe_for_print(x) (float)((x) != (x) ? -12301.0 : ((x) == infinity() ? -12302.0 : ((x) == -infinity() ? -12303.0 : (x))))
// want typeof, but don't want to change build to use new C version at this point
#define check_double(a, b) ({if (!(a == b || abs((a)-(b)) < 1e-11f)) printf("%f != %f %s\n", safe_for_print(a), safe_for_print(b), __STRING((b))); ASSERT(a == b || abs((a)-(b)) < 1e-11f); })
#define check_int(a, b) ({if ((a)!=(b)) printf("%d != %d %s\n", a, b, __STRING((b))); ASSERT((a) == (b)); })
#define check_uint(a, b) ({if ((a)!=(b)) printf("%u != %u %s\n", a, b, __STRING((b))); ASSERT((a) == (b)); })
#define check_int64(a, b) ({if ((a)!=(b)) printf("%08x%08x != %08x%08x %s\n", (int)(a>>32), (int)a, (int)(b>>32), (int)b, __STRING((b))); ASSERT((a) == (b)); })
#define check_uint64(a, b) ({if ((a)!=(b)) printf("%08x%08x != %08x%08x %s\n", (int)(a>>32), (int)a, (int)(b>>32), (int)b, __STRING((b))); ASSERT((a) == (b)); })

#define check_double_fn1(fn, a) check_double(fn(a), mufp_funcs->mufp_##fn(a))
#define check_double_fn2(fn, a, b) check_double(fn(a, b), mufp_funcs->mufp_##fn(a, b))
#define check_int_fn1(fn, a) check_int(fn(a), mufp_funcs->mufp_##fn(a))
#define check_int64_fn1(fn, a) check_int64(fn(a), mufp_funcs->mufp_##fn(a))
#define check_int_fn2(fn, a, b) check_int(fn(a, b), mufp_funcs->mufp_##fn(a, b))
#define check_int64_fn2(fn, a, b) check_int64(fn(a, b), mufp_funcs->mufp_##fn(a, b))
#define check_uint_fn1(fn, a) check_uint(fn(a), mufp_funcs->mufp_##fn(a))
#define check_uint64_fn1(fn, a) check_uint64(fn(a), mufp_funcs->mufp_##fn(a))
#define check_uint_fn2(fn, a, b) check_uint(fn(a, b), mufp_funcs->mufp_##fn(a, b))
#define check_uint64_fn2(fn, a, b) check_uint64(fn(a, b), mufp_funcs->mufp_##fn(a, b))

int __attribute__((naked)) dcmp_from_dcmp_flags(double a, double b, int (*dmcp_flags)(double, double)) {
    asm(
        "push {r4, r5, lr}\n"
        "mov r4, #1\n"
        "ldr r5, [sp, #0xc]\n" // dcmp_flags param
        "blx r5\n"
        "bge 1f\n"
        "neg r4, r4\n"
        "1:\n"
        "bne 1f\n"
        "sub r4, r4\n"
        "1:\n"
        "mov r0, r4\n"
        "pop {r4, r5, pc}\n"
    );
}

double __attribute__((naked)) call_dsincos(double v, double *cout, double (*func)(double)) {
    asm(
    "push {r2, r4, lr}\n"
    "blx r3\n"
    "pop {r4}\n"
    "stmia r4!, {r2, r3}\n"
    "pop {r4, pc}\n"
    );
}

#define check_dcmp_flags(a,b) check_int(dcmp(a, b), dcmp_from_dcmp_flags(a, b, (void (*)(double,double))mufp_funcs->mufp_dcmp)) // dcmp is dcmp_flags now
#define check_dcmp_fast_flags(a,b) check_int(dcmp_fast(a, b), dcmp_from_dcmp_flags(a, b, mufp_funcs->mufp_dcmp_fast_flags))

int main()
{
    setup_default_uart();
    int rom_version = *(uint8_t*)0x13;
    printf("ROM VERSION %d\n", rom_version);
    if (rom_version == 1) {
        printf("ROM VERSION 1 HAS NO DOUBLE, SKIPPING\n");
        exit(0);
    }

    srand(0xf005ba11);
    mufp_funcs = (struct mufp_funcs *)rom_data_lookup(rom_table_code('S','D'));
    ASSERT(mufp_funcs);

    uint8_t *func_count = (uint8_t *)rom_data_lookup(rom_table_code('F','Z'));
    assert(func_count);
    assert(*func_count == sizeof(struct mufp_funcs) / 4);
    for(int i=0; i<sizeof(struct mufp_funcs); i+=4) {
        uint32_t fp = *(uint32_t*)(((uint8_t*)mufp_funcs) + i);
        ASSERT(fp);
        ASSERT(fp & 1u); // thumb bit!
        ASSERT(fp < 16 * 1024); // in ROM!
    }

    // very simple sanity tests
    check_double_fn2(dadd, 1.3, -5.0);

    check_double_fn2(dsub, 1000.75, 998.6);

    check_double_fn2(dmul, 1.75, 31.4);

    check_double_fn2(ddiv, 2314.6, -.37);
    check_double_fn2(ddiv, 234.6, -10000.37);
    check_double_fn2(ddiv, 2314.6, infinity());
    check_double_fn2(ddiv, 2314.6, -infinity());

    if (rom_version > 1) {
        // todo check denormals
        check_int_fn2(dcmp, -3.0, 7.3);
        check_int_fn2(dcmp, 3.0, -7.3);
        check_int_fn2(dcmp, 3.0, 3.0);
        check_int_fn2(dcmp, 3.0, -infinity());
        check_int_fn2(dcmp, 3.0, infinity());

        check_int_fn2(dcmp, least_neg_denorm.d, most_neg_denorm.d);
        check_int_fn2(dcmp, most_neg_denorm.d, least_neg_denorm.d);
        check_int_fn2(dcmp, least_neg_denorm.d, least_neg_denorm.d);
        check_int_fn2(dcmp, least_neg_norm.d, least_neg_denorm.d);
        check_int_fn2(dcmp, least_neg_denorm.d, least_neg_denorm2.d);
        check_int_fn2(dcmp, least_neg_denorm.d, least_pos_denorm.d);
        check_int_fn2(dcmp, least_pos_denorm.d, most_pos_denorm.d);
        check_int_fn2(dcmp, most_pos_denorm.d, least_pos_denorm.d);
        check_int_fn2(dcmp, least_pos_denorm.d, least_pos_denorm.d);
        check_int_fn2(dcmp, least_pos_denorm.d, least_pos_denorm.d);
        check_int_fn2(dcmp, least_pos_norm.d, least_pos_denorm.d);
        check_int_fn2(dcmp, least_pos_denorm.d, least_pos_denorm2.d);
        check_int_fn2(dcmp, least_pos_denorm.d, least_neg_denorm.d);
    }

    check_int_fn2(dcmp_fast, -3.0, 7.3);
    check_int_fn2(dcmp_fast, 3.0, -7.3);
    check_int_fn2(dcmp_fast, 3.0, 3.0);
    check_int_fn2(dcmp_fast, 3.0, -infinity());
    check_int_fn2(dcmp_fast, 3.0, infinity());

    check_int_fn2(dcmp_fast, least_neg_denorm.d, most_neg_denorm.d);
    check_int_fn2(dcmp_fast, most_neg_denorm.d, least_neg_denorm.d);
    check_int_fn2(dcmp_fast, least_neg_denorm.d, least_neg_denorm.d);
    check_int_fn2(dcmp_fast, least_neg_norm.d, least_neg_denorm.d);
    check_int_fn2(dcmp_fast, least_neg_denorm.d, least_neg_denorm2.d);
    check_int_fn2(dcmp_fast, least_neg_denorm.d, least_pos_denorm.d);
    check_int_fn2(dcmp_fast, least_pos_denorm.d, most_pos_denorm.d);
    check_int_fn2(dcmp_fast, most_pos_denorm.d, least_pos_denorm.d);
    check_int_fn2(dcmp_fast, least_pos_denorm.d, least_pos_denorm.d);
    check_int_fn2(dcmp_fast, least_pos_denorm.d, least_pos_denorm.d);
    check_int_fn2(dcmp_fast, least_pos_norm.d, least_pos_denorm.d);
    check_int_fn2(dcmp_fast, least_pos_denorm.d, least_pos_denorm2.d);
    check_int_fn2(dcmp_fast, least_pos_denorm.d, least_neg_denorm.d);


    if (rom_version > 1) {
        // todo check denormals
        check_dcmp_flags(-3.0, 7.3);
        check_dcmp_flags(3.0, -7.3);
        check_dcmp_flags(3.0, 3.0);
        check_dcmp_flags(3.0, -infinity());
        check_dcmp_flags(3.0, infinity());

        check_dcmp_flags( least_neg_denorm.d, most_neg_denorm.d);
        check_dcmp_flags( most_neg_denorm.d, least_neg_denorm.d);
        check_dcmp_flags( least_neg_denorm.d, least_neg_denorm.d);
        check_dcmp_flags( least_neg_norm.d, least_neg_denorm.d);
        check_dcmp_flags( least_neg_denorm.d, least_neg_denorm2.d);
        check_dcmp_flags( least_neg_denorm.d, least_pos_denorm.d);
        check_dcmp_flags( least_pos_denorm.d, most_pos_denorm.d);
        check_dcmp_flags( most_pos_denorm.d, least_pos_denorm.d);
        check_dcmp_flags( least_pos_denorm.d, least_pos_denorm.d);
        check_dcmp_flags( least_pos_denorm.d, least_pos_denorm.d);
        check_dcmp_flags( least_pos_norm.d, least_pos_denorm.d);
        check_dcmp_flags( least_pos_denorm.d, least_pos_denorm2.d);
        check_dcmp_flags( least_pos_denorm.d, least_neg_denorm.d);
    }

    check_dcmp_fast_flags(-3.0, 7.3);
    check_dcmp_fast_flags(3.0, -7.3);
    check_dcmp_fast_flags(3.0, 3.0);
    check_dcmp_fast_flags(3.0, -infinity());
    check_dcmp_fast_flags(3.0, infinity());

    check_dcmp_fast_flags( least_neg_denorm.d, most_neg_denorm.d);
    check_dcmp_fast_flags( most_neg_denorm.d, least_neg_denorm.d);
    check_dcmp_fast_flags( least_neg_denorm.d, least_neg_denorm.d);
    check_dcmp_fast_flags( least_neg_norm.d, least_neg_denorm.d);
    check_dcmp_fast_flags( least_neg_denorm.d, least_neg_denorm2.d);
    check_dcmp_fast_flags( least_neg_denorm.d, least_pos_denorm.d);
    check_dcmp_fast_flags( least_pos_denorm.d, most_pos_denorm.d);
    check_dcmp_fast_flags( most_pos_denorm.d, least_pos_denorm.d);
    check_dcmp_fast_flags( least_pos_denorm.d, least_pos_denorm.d);
    check_dcmp_fast_flags( least_pos_denorm.d, least_pos_denorm.d);
    check_dcmp_fast_flags( least_pos_norm.d, least_pos_denorm.d);
    check_dcmp_fast_flags( least_pos_denorm.d, least_pos_denorm2.d);
    check_dcmp_fast_flags( least_pos_denorm.d, least_neg_denorm.d);

    check_double_fn1(dsqrt, 3.0);

    // we are returning INFINITE not NAN as we don't support NANs
//    check_double_fn1(fsqrt, -3.0);
    // todo right now qsqrt and fsqrt return opposite signed infinity
#if 0
    ASSERT(infinity() == mufp_funcs->mufp_dsqrt(-3.0));
#else
    ASSERT(-infinity() == mufp_funcs->mufp_dsqrt(-3.0));
#endif

    check_int_fn1(double2int, 3.0);
    check_int_fn1(double2int, 123456000000.0);
    check_int_fn1(double2int, -3.0);
    check_int_fn1(double2int, -123456000000.0);
    check_int_fn1(double2int, infinity());
    check_int_fn1(double2int, -infinity());

    check_int_fn2(double2fix, 3.0, 3);
    check_int_fn2(double2fix, 31.0, -3);
    check_int_fn2(double2fix, -3.0, 3);
    // todo JURY IS OUT ON THIS ONE
    //check_int_fn2(double2fix, -31.0, -3);

    check_uint_fn1(double2uint, 3.0);
    check_uint_fn1(double2uint, 123456000000.0);
    check_uint_fn1(double2uint, -3.0);
    check_uint_fn1(double2uint, -123456000000.0);

    check_uint_fn2(double2ufix, 3.0, 3);
    check_uint_fn2(double2ufix, 3.0, -3);

    check_double_fn1(int2double, 3);
    check_double_fn1(int2double, INT32_MAX);
    check_double_fn1(int2double, INT32_MIN);
    check_double_fn1(int2double, -3);

    check_double_fn2(fix2double, 3, 3);
    check_double_fn2(fix2double, 3, -3);
    check_double_fn2(fix2double, -3, 3);
    check_double_fn2(fix2double, -3, -3);

    check_double_fn1(uint2double, 3);
    check_double_fn1(uint2double, UINT32_MAX);

//    double (*mufp_ufix2double)(uint, int);

    check_double_fn1(dcos, 0.0);
    check_double_fn1(dcos, 2.7);
    check_double_fn1(dcos, -32.7);

    check_double_fn1(dsin, 0.0);
    check_double_fn1(dsin, 2.7);
    check_double_fn1(dsin, -32.7);

    check_double_fn1(dtan, 0.0);
    check_double_fn1(dtan, 2.7);
    check_double_fn1(dtan, -32.7);

    if (rom_version > 1) {
        check_double_fn2(datan2, 3.0, 4.0);
        check_double_fn2(datan2, -3.0, 4.0);
        check_double_fn2(datan2, 4.0, -.31);
        check_double_fn2(datan2, -3.0, -.17);
    }

    check_double_fn1(dexp, 0.0);
    check_double_fn1(dexp, 2.7);
    check_double_fn1(dexp, -32.7);

    check_double_fn1(dln, 0.3);
    check_double_fn1(dln, 1.0);
    check_double_fn1(dln, 2.7);

    // we are returning -INFINITE as we don't support NANs
//    check_double_fn1(fln, -32.7);
    ASSERT(-INFINITY == mufp_funcs->mufp_dln(-32.7));

    check_int64_fn1(double2int64, 3.0);
    check_int64_fn1(double2int64, 123456000000.0);
    check_int64_fn1(double2int64, 12345678912345.0);
    check_int64_fn1(double2int64, -3.0);
    check_int64_fn1(double2int64, -123456000000.0);
    check_int64_fn1(double2int64, -12345678912345.0);

    // seems like gcc is wrong on this one
//        check_int64_fn1(double2int64, INFINITY);
    // so
    ASSERT(INT64_MAX == mufp_funcs->mufp_double2int64(INFINITY));

    // seems like gcc is wrong on this one
//        check_int64_fn1(double2int64, -INFINITY);
    // so
    ASSERT( INT64_MIN == mufp_funcs->mufp_double2int64(-INFINITY));


    check_int64_fn2(double2fix64, 3.0, 3);
    check_int64_fn2(double2fix64, 31.0, -3);
    check_int64_fn2(double2fix64, -3.0, 3);
    // todo JURY IS OUT ON THIS ONE
    //check_int64_fn2(double2fix64, -31.0, -3);

    check_uint64_fn1(double2uint64, 3.0);
    check_uint64_fn1(double2uint64, 123456000000.0);
    check_uint64_fn1(double2uint64, 12345678912345.0);
    check_uint64_fn1(double2uint64, -3.0);
    check_uint64_fn1(double2uint64, -123456000000.0);
    check_uint64_fn1(double2uint64, -12345678912345.0);

    check_uint64_fn2(double2ufix64, 3.0, 3);
    check_uint64_fn2(double2ufix64, 3.0, 43);
    check_uint64_fn2(double2ufix64, 3.0, -3);
    check_uint64_fn2(double2ufix64, 3.0, -43);

#define LARGE 0x1234567800000000ll
    check_double_fn1(int642double, 3);
    check_double_fn1(int642double, LARGE);
    check_double_fn1(int642double, INT32_MAX);
    check_double_fn1(int642double, INT64_MAX);
    check_double_fn1(int642double, INT32_MIN);
    check_double_fn1(int642double, INT64_MIN);
    check_double_fn1(int642double, -3);
    check_double_fn1(int642double, -LARGE);

    check_double_fn2(fix642double, 3, 3);
    check_double_fn2(fix642double, 3, -3);
    check_double_fn2(fix642double, -3, 3);
    check_double_fn2(fix642double, -3, -3);

    check_double_fn2(fix642double, LARGE, 3);
    check_double_fn2(fix642double, LARGE, -3);
    check_double_fn2(fix642double, -LARGE, 3);
    check_double_fn2(fix642double, -LARGE, -3);

    check_double_fn1(uint642double, 3);
    check_double_fn1(uint642double, UINT32_MAX);
    check_double_fn1(uint642double, UINT64_MAX);
    check_double_fn1(uint642double, LARGE);

    check_double_fn1(double2float, 3.0f);
    check_double_fn1(double2float, 3.0e12f);
    check_double_fn1(double2float, -3.0e12f);
    check_double_fn1(double2float, -3.13159275412321412565856845745);
    check_double_fn1(double2float, 3.13159275412321412565856845745);
    check_double_fn1(double2float, -INFINITY);
    check_double_fn1(double2float, INFINITY);
    check_double_fn1(double2float, most_pos_denorm.d);
    check_double_fn1(double2float, least_pos_denorm.d);
    check_double_fn1(double2float, least_pos_norm.d);
    check_double_fn1(double2float, least_neg_denorm.d);
    check_double_fn1(double2float, least_neg_norm.d);
    check_double_fn1(double2float, most_neg_denorm.d);
    check_double_fn1(double2float, 0.f);
    check_double_fn1(double2float, -0.f);

    check_double_fn1(double2float, 3.0e-58);
    check_double_fn1(double2float, 3.0e58);

    check_double_fn1(double2float, 3.0e68);
    check_double_fn1(double2float, -3.0e68);

    if (rom_version >= 3) {
        for(double a = -0.3f; a<7.f; a += 0.137) {
            double s = mufp_funcs->mufp_dsin(a);
            double c = mufp_funcs->mufp_dcos(a);
            double co=0;
            double so = call_dsincos(a, &co, mufp_funcs->v3_mufp_dsincos);
            ASSERT(so == s && co == c);
        }
    }
    printf("DOUBLE OK\n");
    return 0;
}
