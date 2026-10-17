/* ESP32-buddy 主机模拟器（D 类工具，非设备证据）。
 * 把 pet_state 的 6 个状态各渲染一帧，输出 Windows 可直接打开的 BMP。
 * 复用固件的 pet_render / pet_state（与设备同一份代码）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pet_link.h"
#include "pet_render.h"
#include "pet_state.h"

#define W 135
#define H 240
#define ORIGIN_X 19
#define ORIGIN_Y 56
#define BG 0x0841u /* 深灰蓝底，便于看清宠物轮廓 */

static uint8_t g_fb[W * H * 2];
static pet_surface_t g_surface;

static void surface_init(void)
{
    g_surface.pixels = g_fb;
    g_surface.width = W;
    g_surface.height = H;
    g_surface.stride = W * 2;
}

static void put(uint8_t *pixels, int stride, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= W || y >= H) {
        return;
    }
    uint8_t *p = pixels + (size_t)y * stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF);
    p[1] = (uint8_t)(c >> 8);
}

static void fill_bg(uint16_t c)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            put(g_fb, g_surface.stride, x, y, c);
        }
    }
}

static uint16_t get_px(const uint8_t *pixels, int stride, int x, int y)
{
    const uint8_t *p = pixels + (size_t)y * stride + (size_t)x * 2;
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* ---- 24-bit BMP（无压缩、bottom-up、行 4 字节对齐）---- */

static void write_bmp(const char *path, const uint8_t *rgb, int w, int h)
{
    const int row = (w * 3 + 3) & ~3;
    const uint32_t imgsize = (uint32_t)row * (uint32_t)h;
    const uint32_t filesize = 54u + imgsize;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    memcpy(hdr + 2, &filesize, 4);
    uint32_t off = 54;
    memcpy(hdr + 10, &off, 4);
    uint32_t hs = 40;
    memcpy(hdr + 14, &hs, 4);
    int32_t ww = w, hh = h;
    memcpy(hdr + 18, &ww, 4);
    memcpy(hdr + 22, &hh, 4);
    uint16_t planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &imgsize, 4);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    fwrite(hdr, 1, 54, f);
    uint8_t *line = calloc(1, (size_t)row);
    for (int y = h - 1; y >= 0; y--) { /* bottom-up */
        for (int x = 0; x < w; x++) {
            const uint8_t *p = rgb + ((size_t)y * w + x) * 3;
            line[x * 3 + 0] = p[2]; /* B */
            line[x * 3 + 1] = p[1]; /* G */
            line[x * 3 + 2] = p[0]; /* R */
        }
        fwrite(line, 1, (size_t)row, f);
    }
    free(line);
    fclose(f);
}

static void surface_to_rgb(uint8_t *rgb)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint16_t c = get_px(g_fb, g_surface.stride, x, y);
            const uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
            const uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
            const uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
            uint8_t *p = rgb + ((size_t)y * W + x) * 3;
            p[0] = r;
            p[1] = g;
            p[2] = b;
        }
    }
}

/* ---- 各状态 ---- */

static void drive(struct pet_state *s, pet_event_kind_t k, uint32_t now)
{
    pet_event_t e;
    memset(&e, 0, sizeof e);
    e.kind = k;
    e.now_ms = now;
    pet_state_apply(s, &e);
}

/* 事件 a→b 到达某状态；tick_at>0 时先保活再 tick（驱动 idle 动画） */
static pet_view_t view_for(pet_event_kind_t a, pet_event_kind_t b, uint32_t tick_at)
{
    struct pet_state *s = pet_state_create();
    if (a != PET_EV_CARD) {
        drive(s, a, 1000);
    }
    if (b != PET_EV_CARD) {
        drive(s, b, 2000);
    }
    if (tick_at != 0) {
        drive(s, PET_EV_ANNOUNCEMENT, tick_at); /* 保活，避免判失联 */
        pet_state_tick(s, tick_at);
    }
    pet_view_t v;
    memset(&v, 0, sizeof v);
    pet_state_view(s, &v);
    pet_state_destroy(s);
    return v;
}

int main(int argc, char **argv)
{
    const char *look_path = (argc > 1) ? argv[1]
                                       : "firmware/components/pet/assets/robot.look";
    FILE *lf = fopen(look_path, "rb");
    if (lf == NULL) {
        fprintf(stderr, "cannot open look: %s\n", look_path);
        return 2;
    }
    fseek(lf, 0, SEEK_END);
    long len = ftell(lf);
    fseek(lf, 0, SEEK_SET);
    uint8_t *look_bytes = malloc((size_t)len);
    if (fread(look_bytes, 1, (size_t)len, lf) != (size_t)len) {
        return 2;
    }
    fclose(lf);

    pet_look_t *look = pet_look_parse(look_bytes, (size_t)len);
    if (look == NULL) {
        fprintf(stderr, "look parse failed\n");
        return 2;
    }

    surface_init();

    /* 状态 → (事件 a, 事件 b, tick)；tick>0 用于 idle 眨眼/发呆 */
    const struct {
        const char *name;
        pet_event_kind_t a, b;
        uint32_t tick_at;
    } cases[] = {
        {"01-idle", PET_EV_AGENT_IDLE, PET_EV_CARD, 0},
        {"01b-idle-blink", PET_EV_AGENT_IDLE, PET_EV_CARD, 1900},
        {"01c-idle-nap", PET_EV_AGENT_IDLE, PET_EV_CARD, 20000},
        {"02-working", PET_EV_TASK_START, PET_EV_CARD, 0},
        {"03-input", PET_EV_TASK_START, PET_EV_AGENT_INPUT_REQUIRED, 0},
        {"04-done", PET_EV_TASK_START, PET_EV_TASK_DONE, 0},
        {"05-failed", PET_EV_TASK_START, PET_EV_TASK_ERROR, 0},
        {"06-offline", PET_EV_TASK_START, PET_EV_LINK_DOWN, 0},
    };
    const int ncases = (int)(sizeof cases / sizeof cases[0]);

    uint8_t *sheet = calloc((size_t)W * ncases * H * 3, 1);
    if (sheet == NULL) {
        return 2;
    }

    for (int i = 0; i < ncases; i++) {
        const pet_view_t v = view_for(cases[i].a, cases[i].b, cases[i].tick_at);

        fill_bg(BG);
        pet_render_pet(&v, look, &g_surface, ORIGIN_X, ORIGIN_Y);

        char path[256];
        snprintf(path, sizeof path, "sim/%s.bmp", cases[i].name);
        uint8_t *rgb = malloc((size_t)W * H * 3);
        surface_to_rgb(rgb);
        write_bmp(path, rgb, W, H);
        /* 拼到总览（横向排列） */
        for (int y = 0; y < H; y++) {
            memcpy(sheet + (((size_t)y * W * ncases) + (size_t)i * W) * 3,
                   rgb + (size_t)y * W * 3, (size_t)W * 3);
        }
        free(rgb);
        printf("wrote %s (state=%d frame=%d)\n", path, (int)v.state, (int)v.frame);
    }

    write_bmp("sim/all-states.bmp", sheet, W * ncases, H);

    /* M3：整屏场景（宠物 + 1 张任务卡） */
    uint8_t *ssheet = calloc((size_t)W * ncases * H * 3, 1);
    for (int i = 0; i < ncases; i++) {
        pet_view_t v = view_for(cases[i].a, cases[i].b, cases[i].tick_at);
        v.has_card = (i != 0); /* idle 不放卡 */
        if (v.has_card) {
            v.card.state = v.state;
            v.card.elapsed_s = 125;
            snprintf(v.card.title, sizeof v.card.title, "BUILD FIRMWARE");
            snprintf(v.card.project, sizeof v.card.project, "ESP32-buddy");
        }
        pet_render_scene(&v, look, &g_surface);
        char path[256];
        snprintf(path, sizeof path, "sim/scene-%s.bmp", cases[i].name);
        uint8_t *rgb = malloc((size_t)W * H * 3);
        surface_to_rgb(rgb);
        write_bmp(path, rgb, W, H);
        if (ssheet != NULL) {
            for (int y = 0; y < H; y++) {
                memcpy(ssheet + (((size_t)y * W * ncases) + (size_t)i * W) * 3,
                       rgb + (size_t)y * W * 3, (size_t)W * 3);
            }
        }
        free(rgb);
    }
    if (ssheet != NULL) {
        write_bmp("sim/all-scenes.bmp", ssheet, W * ncases, H);
        free(ssheet);
    }

    /* M4：示例事件线 → 状态机 → 整屏场景 */
    {
        const char *lines[] = {
            "{\"version\":1,\"event\":\"task.start\",\"title\":\"BUILD FIRMWARE\",\"project\":\"ESP32-buddy\",\"elapsed_s\":75}",
            "{\"version\":1,\"event\":\"agent.input_required\",\"title\":\"REVIEW PR\",\"project\":\"ESP32-buddy\",\"elapsed_s\":900}",
            "{\"version\":1,\"event\":\"task.done\",\"title\":\"RUN TESTS\",\"project\":\"ESP32-buddy\",\"elapsed_s\":120}",
            "{\"version\":1,\"event\":\"task.error\",\"title\":\"FLASH DEVICE\",\"project\":\"ESP32-buddy\",\"elapsed_s\":30}",
        };
        for (int i = 0; i < (int)(sizeof lines / sizeof lines[0]); i++) {
            pet_link_msg_t msg;
            const pet_link_status_t st = pet_link_parse(lines[i], strlen(lines[i]), &msg);
            if (st != PET_LINK_OK) {
                printf("link parse failed (%d)\n", (int)st);
                continue;
            }
            struct pet_state *ls = pet_state_create();
            pet_event_t e;
            memset(&e, 0, sizeof e);
            e.kind = msg.kind;
            e.now_ms = 1000;
            pet_state_apply(ls, &e);
            if (msg.has_card) {
                pet_state_set_card(ls, &msg.card);
            }
            pet_view_t v;
            pet_state_view(ls, &v);
            pet_state_destroy(ls);

            pet_render_scene(&v, look, &g_surface);
            char path[64];
            snprintf(path, sizeof path, "sim/link-%d.bmp", i);
            uint8_t *rgb = malloc((size_t)W * H * 3);
            surface_to_rgb(rgb);
            write_bmp(path, rgb, W, H);
            free(rgb);
            printf("wrote %s (kind=%d card=%d title=%s)\n", path, (int)v.state,
                   (int)v.has_card, v.has_card ? v.card.title : "-");
        }
    }

    /* 立绘 6 帧单图（验美术） */
    for (int fr = 0; fr < PET_FRAME_COUNT; fr++) {
        pet_view_t v;
        memset(&v, 0, sizeof v);
        v.state = PET_STATE_IDLE;
        v.frame = (pet_frame_t)fr;
        fill_bg(BG);
        pet_render_pet(&v, look, &g_surface, ORIGIN_X, ORIGIN_Y);
        char path[64];
        snprintf(path, sizeof path, "sim/look-frame-%d.bmp", fr);
        uint8_t *rgb = malloc((size_t)W * H * 3);
        surface_to_rgb(rgb);
        write_bmp(path, rgb, W, H);
        free(rgb);
    }

    free(sheet);
    pet_look_free(look);
    free(look_bytes);
    printf("done.\n");
    return 0;
}
