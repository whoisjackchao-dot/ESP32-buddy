/* M8 多卡紧凑版式的离线布局断言（T 类；不替代真机 H）。
 * 目的：在无设备时锁住 3 行卡区的行高、左右边界和底部留白，
 * 防止后续 UI 改动把内容画到屏幕外或压到屏幕边缘。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pet_render.h"

#define SW 135
#define SH 240
#define BG PET_SCENE_BG

static uint8_t fb[SW * SH * 2];
static pet_surface_t surface;
static pet_look_t *g_look;
static int checks;
static int failed;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static uint16_t px(int x, int y)
{
    const uint8_t *p = fb + (size_t)y * surface.stride + (size_t)x * 2;
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int count_non(int x0, int y0, int x1, int y1, uint16_t c)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (px(x, y) != c) {
                n++;
            }
        }
    }
    return n;
}

static pet_view_t three_cards(void)
{
    pet_view_t v;
    memset(&v, 0, sizeof v);
    v.state = PET_STATE_WORKING;
    v.frame = PET_FRAME_WORKING;
    v.has_card = true;
    v.card_count = 3;
    for (int i = 0; i < 3; i++) {
        v.cards[i].state = PET_STATE_WORKING;
        v.cards[i].elapsed_s = 3600 + i;
        snprintf(v.cards[i].title, sizeof v.cards[i].title,
                 "TASK %d WITH A VERY LONG TITLE", i + 1);
        snprintf(v.cards[i].project, sizeof v.cards[i].project,
                 "ESP32-buddy-project-%d", i + 1);
    }
    v.card = v.cards[0];
    return v;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1]
                                  : "firmware/components/pet/assets/robot.look";
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open fixture: %s\n", path);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = malloc((size_t)n);
    if (bytes == NULL || fread(bytes, 1, (size_t)n, f) != (size_t)n) {
        return 2;
    }
    fclose(f);
    g_look = pet_look_parse(bytes, (size_t)n);
    if (g_look == NULL) {
        return 2;
    }

    surface.pixels = fb;
    surface.width = SW;
    surface.height = SH;
    surface.stride = SW * 2;

    pet_view_t v = three_cards();
    pet_render_scene(&v, g_look, &surface);

    /* 3 行卡区：y=150/178/206，每行 28px；每行左侧状态条必须存在。 */
    for (int i = 0; i < 3; i++) {
        const int y = 150 + i * 28;
        CHECK(count_non(4, y, 6, y + 27, BG) > 0);
        CHECK(count_non(11, y + 2, 131, y + 26, BG) > 0);
    }

    /* 卡区左右不越界（x=0..3 / x=132..134 在卡区应为底色）。 */
    CHECK(count_non(0, 150, 3, 235, BG) == 0);
    CHECK(count_non(132, 150, 134, 235, BG) == 0);

    /* 最后一行下沿 233，234..239 仍为底色。 */
    CHECK(count_non(0, 234, 134, 239, BG) == 0);

    /* 长 project/title 不允许把内容画到屏幕外（guard 区域）。 */
    CHECK(count_non(0, 236, 134, 239, BG) == 0);

    pet_look_free(g_look);
    free(bytes);
    printf("%d checks, %d failed\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
