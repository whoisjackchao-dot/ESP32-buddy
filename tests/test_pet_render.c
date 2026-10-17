/* M1/M2 T 类：pet_render 的 RED→GREEN。
 * 断言体现业务规则（拒绝伪造包、按 2× 绘制不透明像素、跳过透明像素、越界裁剪）。
 * 期望值由**测试自己独立解析立绘包**得出（不调用 pet_render 的内部实现），
 * 因此换美术不影响本测试。契约：contracts/M1-渲染分层契约冻结.md。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "pet_render.h"

#ifndef PET_FIXTURE
#define PET_FIXTURE "firmware/components/pet/assets/robot.look"
#endif

#define SW 100
#define SH 132
#define GUARD 16
#define SENTINEL 0xF81Fu /* 品红，用来区分"未绘制"与"画成透明色" */

#define ORIGIN_X_FOR_TEST 19
#define ORIGIN_Y_FOR_TEST 56

/* 立绘包格式常量（与 tools/make-look.py 的规格一致；测试侧独立实现） */
#define LOOK_W 48
#define LOOK_H 64
#define LOOK_FRAMES 4
#define LOOK_COLORS 16
#define LOOK_FRAME_BYTES (LOOK_W * LOOK_H / 2)
#define LOOK_HEADER (8 + LOOK_COLORS * 2)
#define LOOK_BYTES (LOOK_HEADER + LOOK_FRAMES * LOOK_FRAME_BYTES)

static uint8_t  fb[SW * SH * 2 + GUARD];
static pet_surface_t surface;
static uint8_t *g_bytes;
static size_t   g_len;
static pet_look_t *g_look;

/* ---- 测试侧独立解析（黄金期望不来自待测代码）---- */

static uint8_t look_index(const uint8_t *b, int frame, int x, int y)
{
    const int at = y * LOOK_W + x;
    const uint8_t packed = b[LOOK_HEADER + frame * LOOK_FRAME_BYTES + at / 2];
    return (at % 2 == 0) ? (uint8_t)(packed & 0x0F) : (uint8_t)(packed >> 4);
}

static uint16_t look_palette(const uint8_t *b, uint8_t idx)
{
    return (uint16_t)(b[8 + 2 * idx] | (b[9 + 2 * idx] << 8));
}

static int find_pixel(int frame, int want_transparent, int *px, int *py)
{
    for (int y = 0; y < LOOK_H; y++) {
        for (int x = 0; x < LOOK_W; x++) {
            const int transparent = (look_index(g_bytes, frame, x, y) == 0);
            if (transparent == want_transparent) {
                *px = x;
                *py = y;
                return 1;
            }
        }
    }
    return 0;
}

/* ---- 像素工具 ---- */

static uint8_t *load_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open fixture: %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "short read\n");
        exit(2);
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void put_px(const pet_surface_t *s, int x, int y, uint16_t c)
{
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF);
    p[1] = (uint8_t)(c >> 8);
}

static uint16_t get_px(const pet_surface_t *s, int x, int y)
{
    const uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void fill(uint16_t c)
{
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            put_px(&surface, x, y, c);
        }
    }
}

static void arm_guard(void)
{
    memset(fb + SW * SH * 2, 0xA5, GUARD);
}

static int guard_intact(void)
{
    for (int i = 0; i < GUARD; i++) {
        if (fb[SW * SH * 2 + i] != 0xA5) {
            return 0;
        }
    }
    return 1;
}

/* ---- 解析类 ---- */

static void parse_rejects_bad_magic(void)
{
    uint8_t *copy = malloc(g_len);
    memcpy(copy, g_bytes, g_len);
    copy[0] = 'X';
    CHECK(pet_look_parse(copy, g_len) == NULL);
    free(copy);
}

static void parse_rejects_short(void)
{
    CHECK(pet_look_parse(g_bytes, g_len - 1) == NULL);
}

static void parse_accepts_valid_pack(void)
{
    pet_look_t *l = pet_look_parse(g_bytes, g_len);
    CHECK(l != NULL);
    pet_look_free(l);
}

/* ---- 绘制类 ---- */

static void draw_paints_opaque_pixel_as_2x_block(void)
{
    int x = 0, y = 0;
    CHECK(find_pixel(0, 0, &x, &y));
    const uint16_t want = look_palette(g_bytes, look_index(g_bytes, 0, x, y));

    fill(SENTINEL);
    pet_view_t v = { .state = PET_STATE_IDLE, .frame = PET_FRAME_NORMAL };
    pet_render_pet(&v, g_look, &surface, 0, 0);

    CHECK(get_px(&surface, x * 2, y * 2) == want);
    CHECK(get_px(&surface, x * 2 + 1, y * 2) == want);
    CHECK(get_px(&surface, x * 2, y * 2 + 1) == want);
    CHECK(get_px(&surface, x * 2 + 1, y * 2 + 1) == want);
}

static void draw_skips_transparent_pixel(void)
{
    int x = 0, y = 0;
    CHECK(find_pixel(0, 1, &x, &y)); /* 找一个透明像素 */

    fill(SENTINEL);
    pet_view_t v = { .state = PET_STATE_IDLE, .frame = PET_FRAME_NORMAL };
    pet_render_pet(&v, g_look, &surface, 0, 0);

    CHECK(get_px(&surface, x * 2, y * 2) == SENTINEL);
    CHECK(get_px(&surface, x * 2 + 1, y * 2 + 1) == SENTINEL);
}

static void draw_clips_at_right_edge(void)
{
    arm_guard();
    fill(SENTINEL);
    pet_view_t v = { .state = PET_STATE_IDLE, .frame = PET_FRAME_NORMAL };
    pet_render_pet(&v, g_look, &surface, SW - 10, 0);
    CHECK(guard_intact());
}

static void draw_clips_at_bottom_edge(void)
{
    arm_guard();
    fill(SENTINEL);
    pet_view_t v = { .state = PET_STATE_IDLE, .frame = PET_FRAME_NORMAL };
    pet_render_pet(&v, g_look, &surface, 0, SH - 10);
    CHECK(guard_intact());
}

static void draw_clips_on_negative_origin(void)
{
    int x = 0, y = 0;
    CHECK(find_pixel(0, 0, &x, &y));
    const uint16_t want = look_palette(g_bytes, look_index(g_bytes, 0, x, y));

    arm_guard();
    fill(SENTINEL);
    pet_view_t v = { .state = PET_STATE_IDLE, .frame = PET_FRAME_NORMAL };
    pet_render_pet(&v, g_look, &surface, -8, -8);
    CHECK(guard_intact());
    CHECK(get_px(&surface, x * 2 - 8, y * 2 - 8) == want);
}

/* ---- M2 渲染半段 ---- */

static int has_pixel_in(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x >= SW || y >= SH) {
                continue;
            }
            if (get_px(&surface, x, y) != SENTINEL) {
                return 1;
            }
        }
    }
    return 0;
}

static void gray_renders_grayscale(void)
{
    int x = 0, y = 0;
    CHECK(find_pixel(0, 0, &x, &y));

    fill(SENTINEL);
    pet_view_t v = { .state = PET_STATE_OFFLINE,
                     .frame = PET_FRAME_EYES_CLOSED,
                     .gray = true };
    pet_render_pet(&v, g_look, &surface, 0, 0);

    const uint16_t c = get_px(&surface, x * 2, y * 2);
    CHECK(c != SENTINEL);
    const int r8 = (((c >> 11) & 0x1F) * 255) / 31;
    const int g8 = (((c >> 5) & 0x3F) * 255) / 63;
    const int b8 = ((c & 0x1F) * 255) / 31;
    CHECK(r8 - g8 <= 6 && g8 - r8 <= 6); /* 三通道相等（容忍 5/6/5 量化） */
    CHECK(g8 - b8 <= 6 && b8 - g8 <= 6);
}

/* 构造一个非对称合成立绘：只有 (0,0) 一个不透明像素 → 翻转必须把它送到 (LOOK_W-1,0)。
 * 真实的占位立绘左右对称，无法区分 mirror，故用合成包。 */
static uint8_t *make_synthetic(uint8_t idx, int px, int py)
{
    static uint8_t buf[LOOK_BYTES];
    memset(buf, 0, sizeof buf);
    memcpy(buf, "LOOK", 4);
    buf[4] = LOOK_W;
    buf[5] = LOOK_H;
    buf[6] = LOOK_FRAMES;
    buf[7] = 0;
    buf[8 + 2 * idx] = 0xE0;
    buf[9 + 2 * idx] = 0x07; /* 0x07E0 绿 */
    const int at = py * LOOK_W + px;
    uint8_t *cell = &buf[LOOK_HEADER + at / 2];
    *cell |= (uint8_t)((at % 2 == 0) ? idx : (idx << 4));
    return buf;
}

static void mirror_flips_asymmetric_pixel(void)
{
    pet_look_t *lk = pet_look_parse(make_synthetic(1, 0, 0), LOOK_BYTES);
    CHECK(lk != NULL);

    pet_view_t v = { .state = PET_STATE_IDLE, .frame = PET_FRAME_NORMAL, .mirror = false };
    fill(SENTINEL);
    pet_render_pet(&v, lk, &surface, 0, 0);
    CHECK(get_px(&surface, 0, 0) == 0x07E0);
    CHECK(get_px(&surface, (LOOK_W - 1) * 2, 0) == SENTINEL);

    fill(SENTINEL);
    v.mirror = true;
    pet_render_pet(&v, lk, &surface, 0, 0);
    CHECK(get_px(&surface, (LOOK_W - 1) * 2, 0) == 0x07E0);
    CHECK(get_px(&surface, 0, 0) == SENTINEL);

    pet_look_free(lk);
}

static void working_and_input_draw_marks_above_head(void)
{
    const int oy = 40; /* 留出头顶空间 */

    fill(SENTINEL);
    pet_view_t v = { .state = PET_STATE_IDLE, .frame = PET_FRAME_NORMAL };
    pet_render_pet(&v, g_look, &surface, 0, oy);
    CHECK(has_pixel_in(0, oy - 20, SW - 1, oy - 2) == 0); /* IDLE 头顶无标记 */

    fill(SENTINEL);
    v.state = PET_STATE_WORKING;
    pet_render_pet(&v, g_look, &surface, 0, oy);
    CHECK(has_pixel_in(0, oy - 20, SW - 1, oy - 2) == 1);

    fill(SENTINEL);
    v.state = PET_STATE_INPUT_REQUIRED;
    pet_render_pet(&v, g_look, &surface, 0, oy);
    CHECK(has_pixel_in(0, oy - 20, SW - 1, oy - 2) == 1);
}

static uint32_t digest_of(pet_state_t state, pet_frame_t frame, bool gray)
{
    fill(SENTINEL);
    pet_view_t v = { .state = state, .frame = frame, .gray = gray };
    pet_render_pet(&v, g_look, &surface, ORIGIN_X_FOR_TEST, ORIGIN_Y_FOR_TEST);
    uint32_t h = 2166136261u;
    for (int i = 0; i < SW * SH * 2; i++) {
        h = (h ^ fb[i]) * 16777619u;
    }
    return h;
}

static void states_render_differently(void)
{
    const uint32_t idle = digest_of(PET_STATE_IDLE, PET_FRAME_NORMAL, false);
    const uint32_t work = digest_of(PET_STATE_WORKING, PET_FRAME_NORMAL, false);
    const uint32_t input = digest_of(PET_STATE_INPUT_REQUIRED, PET_FRAME_NORMAL, false);
    const uint32_t done = digest_of(PET_STATE_DONE, PET_FRAME_HAPPY, false);
    const uint32_t failed = digest_of(PET_STATE_FAILED, PET_FRAME_SAD, false);
    const uint32_t offline = digest_of(PET_STATE_OFFLINE, PET_FRAME_EYES_CLOSED, true);

    CHECK(idle != work);
    CHECK(idle != input);
    CHECK(idle != done);
    CHECK(idle != failed);
    CHECK(idle != offline);
    CHECK(work != input);
    CHECK(done != work);
    CHECK(failed != done);
    CHECK(offline != work);
}

/* ---- M7 增量 2：番茄钟进度环（POMO-05，契约 M7 §7.1）----
 * 小画布 + 哨兵底色：断言只落在 12/3/6/9 点的带上像素与洞内像素。
 * 角度约定：0°=12 点，顺时针（3 点=90°、6 点=180°、9 点=270°）。 */

#define RW 64
#define RH 64
#define RCX 32
#define RCY 32
#define RR 20
#define RT 4
#define RING_COLOR 0xFD20u /* 橙（=契约 7.1 FOCUS 色） */
#define RING_TRACK 0x4208u /* 轨道 */

static uint8_t       rfb[RW * RH * 2];
static pet_surface_t rsurf;

static void ring_fill(uint16_t c)
{
    for (int y = 0; y < RH; y++) {
        for (int x = 0; x < RW; x++) {
            put_px(&rsurf, x, y, c);
        }
    }
}

static void ring05_progress_zero_is_track(void)
{
    ring_fill(SENTINEL);
    pet_render_ring(&rsurf, RCX, RCY, RR, RT, 0, RING_COLOR, RING_TRACK);
    /* 3 点 = 90°：90×1000 > 0×360 → 轨道色 */
    CHECK(get_px(&rsurf, RCX + RR, RCY) == RING_TRACK);
}

static void ring05_quarter_boundary_is_color_start_not_reached(void)
{
    ring_fill(SENTINEL);
    pet_render_ring(&rsurf, RCX, RCY, RR, RT, 250, RING_COLOR, RING_TRACK);
    /* 90° 边界含入：90×1000 <= 250×360 → 已填充 */
    CHECK(get_px(&rsurf, RCX + RR, RCY) == RING_COLOR);
    /* 270° 尚未填充 */
    CHECK(get_px(&rsurf, RCX - RR, RCY) == RING_TRACK);
    /* 12 点 = 0° = 起点，在环带上且从进度一开始就是进度色 */
    CHECK(get_px(&rsurf, RCX, RCY - RR) == RING_COLOR);
}

static void ring05_full_covers_cardinals(void)
{
    ring_fill(SENTINEL);
    pet_render_ring(&rsurf, RCX, RCY, RR, RT, 1000, RING_COLOR, RING_TRACK);
    CHECK(get_px(&rsurf, RCX + RR, RCY) == RING_COLOR); /* 3 点 */
    CHECK(get_px(&rsurf, RCX, RCY + RR) == RING_COLOR); /* 6 点 */
    CHECK(get_px(&rsurf, RCX - RR, RCY) == RING_COLOR); /* 9 点 */
}

static void ring05_hole_inside_untouched(void)
{
    ring_fill(SENTINEL);
    pet_render_ring(&rsurf, RCX, RCY, RR, RT, 1000, RING_COLOR, RING_TRACK);
    /* 环心 (0 < (R-T)²) 与洞内另一点 (T² < (R-T)²) 必须保持哨兵 */
    CHECK(get_px(&rsurf, RCX, RCY) == SENTINEL);
    CHECK(get_px(&rsurf, RCX - RT, RCY) == SENTINEL);
}

/* ---- M7 增量 2：番茄钟整屏视图（契约 M7 §7.1/§7.2）---- */

#define PW 135
#define PH 240

static uint8_t       pfb[PW * PH * 2];
static pet_surface_t psurf;

static void pomo_fill(uint16_t c)
{
    for (int y = 0; y < PH; y++) {
        for (int x = 0; x < PW; x++) {
            put_px(&psurf, x, y, c);
        }
    }
}

static bool pomo_all_bg(void)
{
    for (int y = 0; y < PH; y++) {
        for (int x = 0; x < PW; x++) {
            if (get_px(&psurf, x, y) != PET_SCENE_BG) {
                return false;
            }
        }
    }
    return true;
}

/* 中心文本区在环洞内（d² <= 27²+4² < 42²），白色只可能来自 M:SS 文本 */
static bool pomo_center_text_visible(void)
{
    for (int y = 68; y < 76; y++) {
        for (int x = 40; x < 94; x++) {
            if (get_px(&psurf, x, y) == 0xFFFFu) {
                return true;
            }
        }
    }
    return false;
}

static void pomo_view_null_is_bg_only(void)
{
    pomo_fill(SENTINEL);
    pet_render_pomo(&psurf, NULL);
    CHECK(pomo_all_bg());
}

static void pomo_view_focus_track_ring_and_center_text(void)
{
    const pet_pomo_view_t v = {
        .phase = PET_POMO_FOCUS,
        .remaining_ms = POMO_FOCUS_MS,
        .total_ms = POMO_FOCUS_MS,
    };
    pet_render_pomo(&psurf, &v);
    /* 进度 0：3 点方向 = 轨道色 */
    CHECK(get_px(&psurf, 67 + 48, 72) == 0x4208u);
    /* 12 点起点 = 进度色（FOCUS 橙） */
    CHECK(get_px(&psurf, 67, 72 - 48) == 0xFD20u);
    /* 中心剩余 "25:00" 白字 */
    CHECK(pomo_center_text_visible());
}

static void pomo_view_break_full_ring_green(void)
{
    const pet_pomo_view_t v = {
        .phase = PET_POMO_BREAK,
        .remaining_ms = 0,
        .total_ms = POMO_BREAK_MS,
    };
    pet_render_pomo(&psurf, &v);
    /* 进度 1000‰：整环进度色（BREAK 绿） */
    CHECK(get_px(&psurf, 67 + 48, 72) == 0x07E0u);
    CHECK(get_px(&psurf, 67 - 48, 72) == 0x07E0u);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : PET_FIXTURE;
    g_bytes = load_file(path, &g_len);

    surface.pixels = fb;
    surface.width = SW;
    surface.height = SH;
    surface.stride = SW * 2;

    rsurf.pixels = rfb;
    rsurf.width = RW;
    rsurf.height = RH;
    rsurf.stride = RW * 2;

    psurf.pixels = pfb;
    psurf.width = PW;
    psurf.height = PH;
    psurf.stride = PW * 2;

    RUN(parse_rejects_bad_magic);
    RUN(parse_rejects_short);
    RUN(parse_accepts_valid_pack);

    g_look = pet_look_parse(g_bytes, g_len);
    if (g_look != NULL) {
        RUN(draw_paints_opaque_pixel_as_2x_block);
        RUN(draw_skips_transparent_pixel);
        RUN(draw_clips_at_right_edge);
        RUN(draw_clips_at_bottom_edge);
        RUN(draw_clips_on_negative_origin);
        RUN(gray_renders_grayscale);
        RUN(mirror_flips_asymmetric_pixel);
        RUN(working_and_input_draw_marks_above_head);
        RUN(states_render_differently);
        pet_look_free(g_look);
    } else {
        CHECK(g_look != NULL); /* 合法包解析不出来本身就是一条失败 */
    }

    RUN(ring05_progress_zero_is_track);
    RUN(ring05_quarter_boundary_is_color_start_not_reached);
    RUN(ring05_full_covers_cardinals);
    RUN(ring05_hole_inside_untouched);
    RUN(pomo_view_null_is_bg_only);
    RUN(pomo_view_focus_track_ring_and_center_text);
    RUN(pomo_view_break_full_ring_green);

    free(g_bytes);
    REPORT();
}
