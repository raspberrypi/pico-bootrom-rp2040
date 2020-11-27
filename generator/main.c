/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>

#include "../usb_device_tiny/scsi_ir.h"

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

typedef unsigned int uint;

static int decompress(const uint8_t *data, int data_size, uint8_t *buf, int buf_size);
static void dump(const char *name, char *suffix, const uint8_t *buf, size_t n);

// NOTE: This is a VERY simple and HIGHLY specialized compression format.
// It can compress a string of bytes (which contain only 0 <= c <= 127)...
//
// The goals were simplicity/size of decode function and the ability to
// handle repeated strings in the welcome.html
int compress(const uint8_t *src, int len, const char *name) {
    static uint8_t buf[4096];
    int cost = len;
    int n = 0;
    for (int i = 0; i < len; i++) {
        if (src[i] >= 0x80) {
            fprintf(stderr, "Input data may not have top bit set");
            return 1;
        }
    }
    for (int to = 0; to < len; to++) {
        bool found = false;
        for (int match_len = MIN(127, len - to); match_len >= 3 && !found; match_len--) {
            for (int from = MAX(0, to - 255); from < to && !found; from++) {
                if (!memcmp(src + to, src + from, match_len)) {
                    assert(from < to);
//                    printf("Match %d from %d+%d\n", i, k, match_len);
                    // todo look for other matches?
                    cost += 2;
                    cost -= match_len;
                    buf[n++] = 0x100 - match_len;
                    buf[n++] = to - from;
                    to += match_len;
                    found = true;
                }
            }
        }
        if (to != len)
            buf[n++] = src[to];
    }
    static uint8_t check_buf[4096];
    int check_n = decompress(buf, n, check_buf, sizeof(check_buf));
    if (check_n != len || memcmp(src, check_buf, len)) {
        fprintf(stderr, "Decompress check failed\n");
        dump("expected", "", src, len);
        dump("actual", "", check_buf, check_n);
        return 1;
    }

    printf("#ifdef COMPRESS_TEXT\n");
    printf("// %s:\n", name);
    printf("// %d/%d %d %d\n", cost, len, len - cost, n);
    dump(name, "_z", buf, n);
    printf("#define %s_len %d\n", name, len);
    printf("#endif\n\n");
    return 0;
}

int decompress(const uint8_t *data, int data_size, uint8_t *buf, int buf_size) {
    int n = 0;
    for (int i = 0; i < data_size && n < buf_size; i++) {
        uint8_t b = data[i];
        if (b < 0x80u) {
            buf[n++] = b;
        } else {
            int len = 0x100 - b;
            int off = n - data[++i];
            while (len--) {
                buf[n++] = buf[off++];
            }
        }
    }
    if (n == buf_size) n = 0;
    return n;
}

void dump(const char *name, char *suffix, const uint8_t *buf, size_t n) {
    printf("static const uint8_t %s%s[] = {\n", name, suffix);
    for (size_t i = 0; i < n; i += 12) {
        printf("    ");
        for (size_t j = i; j < MIN(n, i + 12); j++) {
            printf("0x%02x, ", buf[j]);
        }
        printf("\n");
    }
    printf("};\n");
    printf("#define %s%s_len %ld\n", name, suffix, n);
}

static char filename[FILENAME_MAX];

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

void replace(char *str, char from, char to) {
    for (char *p = strchr(str, from); p; p = strchr(p, from)) *p = to;
}

int read_fully_and_write_header(char *path, char *fn, uint8_t *buf, uint buf_size, size_t *size_out, bool ifdef) {
    int rc = 0;
    snprintf(filename, sizeof(filename), "%s%s%s", path, PATH_SEPARATOR, fn);
    FILE *in = fopen(filename, "rb");
    if (!in) {
        fprintf(stderr, "Could not open '%s'\n", filename);
        rc = 1;
    }
    size_t size = 0;
    if (!rc) {
        fseek(in, 0, SEEK_END);
        size = ftell(in);
        fseek(in, 0, SEEK_SET);
        if (size > buf_size || 1 != fread(buf, size, 1, in)) {
            fprintf(stderr, "Could not read '%s' size %ld\n", filename, size);
            rc = 1;
        } else {
            if (ifdef) printf("#ifndef COMPRESS_TEXT\n");
            strcpy(filename, fn);
            replace(filename, '.', '_');
            dump(filename, "", buf, size);
            if (ifdef) printf("#endif\n");
            printf("\n");
        }
    }
    if (in) fclose(in);
    *size_out = size;
    return rc;
}

int main(int argc, char **argv) {
    int rc = 0;
    struct stat sb;
    if (argc != 2 || stat(argv[1], &sb) || !(sb.st_mode & S_IFDIR)) {
        fprintf(stderr, "expected valid source path argument.");
        rc = 1;
    }

    static uint8_t buf[4096];

    if (!rc) {
        size_t welcome_html_len;
        rc = read_fully_and_write_header(argv[1], "welcome.html", buf, sizeof(buf), &welcome_html_len, true);
        if (!rc) {
            rc = compress(buf, welcome_html_len, "welcome_html");
        }
        if (!rc) {
            for (uint i = 0; i < welcome_html_len - 12; i++) {
                if (!memcmp(buf + i, "aabbccddeeff", 12)) {
                    static int x;
                    printf("#define welcome_html_version_offset_%d %d\n", ++x, i);
                }
            }
            printf("\n");
        }
    }

    if (!rc) {
        size_t info_uf2_len;
        rc = read_fully_and_write_header(argv[1], "info_uf2.txt", buf, sizeof(buf), &info_uf2_len, false);
    }

    if (!rc) {
        struct scsi_inquiry_response copy = scsi_ir;
        copy.rmb = 0x00, // should be 0x80 but we can't compress that so we use 0 and fill it later
                rc = compress((uint8_t *) &copy, sizeof(copy), "scsi_ir");
    }

    return 0;
}