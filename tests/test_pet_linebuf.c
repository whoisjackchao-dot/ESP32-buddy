/* M4 增量 2b T 类：行缓冲（字节 → 一行 / 超长丢弃）。
 * 契约：contracts/M4-事件入口与OpenCode接入契约清单.md §9.2。 */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "pet_linebuf.h"

static pet_line_feed_t feed_str(pet_linebuf_t *lb, const char *s)
{
    pet_line_feed_t r = PET_LINE_NONE;
    for (const char *p = s; *p; p++) {
        r = pet_linebuf_feed(lb, *p);
    }
    return r;
}

static void lf01_simple_line(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    CHECK(feed_str(&lb, "abc") == PET_LINE_NONE);
    CHECK(pet_linebuf_feed(&lb, '\n') == PET_LINE_READY);
    CHECK(strcmp(lb.buf, "abc") == 0);
    CHECK(lb.len == 3);
}

static void lf02_crlf_stripped(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    CHECK(feed_str(&lb, "abc\r") == PET_LINE_NONE);
    CHECK(pet_linebuf_feed(&lb, '\n') == PET_LINE_READY);
    CHECK(strcmp(lb.buf, "abc") == 0);
}

static void lf03_empty_line(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    CHECK(pet_linebuf_feed(&lb, '\n') == PET_LINE_READY);
    CHECK(lb.buf[0] == '\0');
    CHECK(lb.len == 0);
}

static void lf04_two_lines_reset_between(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    CHECK(feed_str(&lb, "a") == PET_LINE_NONE);
    CHECK(pet_linebuf_feed(&lb, '\n') == PET_LINE_READY);
    CHECK(strcmp(lb.buf, "a") == 0);
    CHECK(feed_str(&lb, "bb") == PET_LINE_NONE);
    CHECK(pet_linebuf_feed(&lb, '\n') == PET_LINE_READY);
    CHECK(strcmp(lb.buf, "bb") == 0);
}

static void lf05_exact_max_ok(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    int all_none = 1;
    for (int i = 0; i < PET_LINE_MAX; i++) {
        if (pet_linebuf_feed(&lb, 'x') != PET_LINE_NONE) {
            all_none = 0;
        }
    }
    CHECK(all_none);
    CHECK(pet_linebuf_feed(&lb, '\n') == PET_LINE_READY);
    CHECK(lb.len == (size_t)PET_LINE_MAX);
    CHECK(lb.buf[PET_LINE_MAX] == '\0');
}

static void lf06_overlong_dropped_then_recovers(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    for (int i = 0; i < PET_LINE_MAX + 5; i++) {
        (void)pet_linebuf_feed(&lb, 'y');
    }
    CHECK(pet_linebuf_feed(&lb, '\n') == PET_LINE_DROPPED); /* 超长整行丢弃 */
    /* 下一行正常 */
    CHECK(feed_str(&lb, "ok") == PET_LINE_NONE);
    CHECK(pet_linebuf_feed(&lb, '\n') == PET_LINE_READY);
    CHECK(strcmp(lb.buf, "ok") == 0);
}

static void lf07_no_newline_no_line(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    CHECK(feed_str(&lb, "{\"version\":1") == PET_LINE_NONE);
}

int main(void)
{
    RUN(lf01_simple_line);
    RUN(lf02_crlf_stripped);
    RUN(lf03_empty_line);
    RUN(lf04_two_lines_reset_between);
    RUN(lf05_exact_max_ok);
    RUN(lf06_overlong_dropped_then_recovers);
    RUN(lf07_no_newline_no_line);
    REPORT();
}
