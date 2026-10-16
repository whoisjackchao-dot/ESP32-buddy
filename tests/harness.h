#pragma once
/* 极简断言 harness：无外部依赖，RED/GREEN 都靠退出码。 */
#include <stdio.h>
#include <string.h>

static int hb_fail = 0;
static int hb_checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        hb_checks++;                                                           \
        if (!(cond)) {                                                         \
            hb_fail++;                                                         \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        fprintf(stderr, "[RUN] %s\n", #fn);                                    \
        fn();                                                                  \
    } while (0)

#define REPORT()                                                               \
    do {                                                                       \
        fprintf(stderr, "%d checks, %d failed\n", hb_checks, hb_fail);         \
        return hb_fail ? 1 : 0;                                                \
    } while (0)
