/* M7 T 类：番茄钟计时状态机 + 当日统计（增量 1）+ 统计持久化编解码（增量 2）。
 * 契约：contracts/M7-番茄钟契约清单.md §6（POMO-01..12）、§7.3（POMO-13）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "pet_pomo.h"

/* POMO-01：init 默认值（IDLE，remaining==total==POMO_FOCUS_MS，计数 0，无铃） */
static void pomo01_init_defaults(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 1000);
    CHECK(p.phase == PET_POMO_IDLE);
    pet_pomo_view_t v;
    pet_pomo_view_get(&p, &v);
    CHECK(v.remaining_ms == POMO_FOCUS_MS);
    CHECK(v.total_ms == POMO_FOCUS_MS);
    CHECK(v.done_count == 0);
    CHECK(v.focus_seconds == 0);
    CHECK(v.alarm == false);
    CHECK(p.alarm == false);
}

/* POMO-02：SHORT → FOCUS 启动；tick(+1000) 后 remaining 减 1000 */
static void pomo02_short_starts_focus(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 100);
    CHECK(p.phase == PET_POMO_FOCUS);
    pet_pomo_tick(&p, 1100);
    pet_pomo_view_t v;
    pet_pomo_view_get(&p, &v);
    CHECK(v.remaining_ms == POMO_FOCUS_MS - 1000);
    CHECK(v.total_ms == POMO_FOCUS_MS);
}

/* POMO-03：暂停不推进，继续后从暂停点接着走 */
static void pomo03_pause_resume(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, 2000); /* 专注 2s */
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 2000);
    CHECK(p.phase == PET_POMO_FOCUS_PAUSED);
    pet_pomo_tick(&p, 7000); /* 暂停期间不推进 */
    CHECK(p.elapsed_ms == 2000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 9000); /* 继续 */
    CHECK(p.phase == PET_POMO_FOCUS);
    pet_pomo_tick(&p, 10000);
    CHECK(p.elapsed_ms == 3000);
}

/* POMO-04：专注到点 → FOCUS_DONE + 响铃 + 统计（1 段 / 1500s），remaining==0 */
static void pomo04_focus_completes(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, POMO_FOCUS_MS + 60000);
    CHECK(p.phase == PET_POMO_FOCUS_DONE);
    CHECK(p.alarm == true);
    CHECK(p.done_count == 1);
    CHECK(p.focus_seconds == POMO_FOCUS_MS / 1000);
    pet_pomo_view_t v;
    pet_pomo_view_get(&p, &v);
    CHECK(v.remaining_ms == 0);
}

/* POMO-05：边界 —— elapsed == total 即到点（>=） */
static void pomo05_boundary_exact(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, POMO_FOCUS_MS - 1);
    CHECK(p.phase == PET_POMO_FOCUS); /* 差 1ms 未到点 */
    pet_pomo_tick(&p, POMO_FOCUS_MS);
    CHECK(p.phase == PET_POMO_FOCUS_DONE);
}

/* POMO-06：DONE 相位不自动推进，也不重复计数 */
static void pomo06_done_is_sticky(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, POMO_FOCUS_MS);
    CHECK(p.phase == PET_POMO_FOCUS_DONE);
    pet_pomo_tick(&p, POMO_FOCUS_MS + 3600000);
    CHECK(p.phase == PET_POMO_FOCUS_DONE);
    CHECK(p.done_count == 1);
}

/* POMO-07：整循环 FOCUS_DONE --SHORT--> BREAK --到点--> BREAK_DONE --SHORT--> IDLE */
static void pomo07_focus_then_break_then_idle(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, POMO_FOCUS_MS);
    CHECK(p.phase == PET_POMO_FOCUS_DONE);
    CHECK(pet_pomo_take_alarm(&p) == true);

    pet_pomo_command(&p, PET_POMO_CMD_SHORT, POMO_FOCUS_MS); /* 休息不自动开始 */
    CHECK(p.phase == PET_POMO_BREAK);
    pet_pomo_view_t v;
    pet_pomo_view_get(&p, &v);
    CHECK(v.total_ms == POMO_BREAK_MS);
    CHECK(v.remaining_ms == POMO_BREAK_MS);

    pet_pomo_tick(&p, POMO_FOCUS_MS + POMO_BREAK_MS);
    CHECK(p.phase == PET_POMO_BREAK_DONE);
    CHECK(p.alarm == true);
    CHECK(p.done_count == 1); /* 休息不计统计 */

    pet_pomo_command(&p, PET_POMO_CMD_SHORT, POMO_FOCUS_MS + POMO_BREAK_MS);
    CHECK(p.phase == PET_POMO_IDLE);
}

/* POMO-08：专注中途长按放弃 → IDLE，不计完成，无铃 */
static void pomo08_abandon_focus(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, 60000); /* 专注 1 分钟 */
    pet_pomo_command(&p, PET_POMO_CMD_LONG, 60000);
    CHECK(p.phase == PET_POMO_IDLE);
    CHECK(p.done_count == 0);
    CHECK(p.focus_seconds == 0);
    CHECK(p.alarm == false);
}

/* POMO-09：响铃取走即清 */
static void pomo09_alarm_take_once(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, POMO_FOCUS_MS);
    CHECK(p.alarm == true);
    CHECK(pet_pomo_take_alarm(&p) == true);
    CHECK(pet_pomo_take_alarm(&p) == false);
    CHECK(p.alarm == false);
}

/* POMO-10：跨日清零；同日重复 set_day 不动 */
static void pomo10_daily_reset(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_set_day(&p, 100);
    CHECK(p.has_day == true);
    CHECK(p.day == 100);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, POMO_FOCUS_MS);
    CHECK(p.done_count == 1);
    CHECK(p.focus_seconds == POMO_FOCUS_MS / 1000);
    pet_pomo_command(&p, PET_POMO_CMD_LONG, 0); /* 回 IDLE（放弃不扣统计） */
    CHECK(p.phase == PET_POMO_IDLE);
    pet_pomo_set_day(&p, 101); /* 换日 → 清零 */
    CHECK(p.done_count == 0);
    CHECK(p.focus_seconds == 0);
    CHECK(p.day == 101);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0); /* 全新一段专注 */
    pet_pomo_tick(&p, POMO_FOCUS_MS);
    CHECK(p.done_count == 1);
    pet_pomo_set_day(&p, 101); /* 同日 → 不动 */
    CHECK(p.done_count == 1);
    CHECK(p.focus_seconds == POMO_FOCUS_MS / 1000);
}

/* POMO-11：IDLE 下长按无变化 */
static void pomo11_long_in_idle_noop(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 123);
    pet_pomo_command(&p, PET_POMO_CMD_LONG, 456);
    CHECK(p.phase == PET_POMO_IDLE);
    CHECK(p.since_ms == 123);
    CHECK(p.elapsed_ms == 0);
    CHECK(p.total_ms == POMO_FOCUS_MS);
}

/* POMO-12：暂停中长按放弃不计；随后 SHORT 全新开始一段 */
static void pomo12_abandon_from_paused(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, 30000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 30000);
    CHECK(p.phase == PET_POMO_FOCUS_PAUSED);
    pet_pomo_command(&p, PET_POMO_CMD_LONG, 40000);
    CHECK(p.phase == PET_POMO_IDLE);
    CHECK(p.done_count == 0);
    CHECK(p.focus_seconds == 0);
    CHECK(p.alarm == false);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 50000); /* 全新一段 */
    CHECK(p.phase == PET_POMO_FOCUS);
    CHECK(p.elapsed_ms == 0);
    pet_pomo_tick(&p, 51000);
    CHECK(p.elapsed_ms == 1000);
}

/* ---- M7 增量 2：统计持久化编解码（POMO-13，契约 §7.3）----
 * 16 字节小端 blob：magic "PMC1"(0..3) | day i32(4..7) | done u32(8..11) | focus_s u32(12..15) */

static void pomo13_encode_layout_is_le_with_magic(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    p.has_day = true;
    p.day = -2;
    p.done_count = 3;
    p.focus_seconds = 4500;

    uint8_t buf[16];
    memset(buf, 0xAA, sizeof buf);
    CHECK(pet_pomo_encode_stats(buf, sizeof buf, &p) == 16);
    CHECK(buf[0] == 'P' && buf[1] == 'M' && buf[2] == 'C' && buf[3] == '1');
    CHECK(buf[4] == 0xFE && buf[5] == 0xFF && buf[6] == 0xFF &&
          buf[7] == 0xFF); /* day=-2 → 0xFFFFFFFE 逐字节小端 */
    CHECK(buf[8] == 3 && buf[9] == 0 && buf[10] == 0 && buf[11] == 0);
    CHECK(buf[12] == 0x94 && buf[13] == 0x11 && buf[14] == 0 &&
          buf[15] == 0); /* 4500 = 0x1194 小端 */
}

static void pomo13_roundtrip_restores_stats_only(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    p.has_day = true;
    p.day = 20260517;
    p.done_count = 7;
    p.focus_seconds = 10500;

    uint8_t buf[PET_POMO_STATS_LEN];
    CHECK(pet_pomo_encode_stats(buf, sizeof buf, &p) == PET_POMO_STATS_LEN);

    pet_pomo_t q;
    pet_pomo_init(&q, 999);
    CHECK(pet_pomo_decode_stats(buf, sizeof buf, &q) == true);
    CHECK(q.has_day == true);
    CHECK(q.day == 20260517);
    CHECK(q.done_count == 7);
    CHECK(q.focus_seconds == 10500);
    /* 成功解码只动统计四件套，计时/相位/铃不动 */
    CHECK(q.phase == PET_POMO_IDLE);
    CHECK(q.elapsed_ms == 0);
    CHECK(q.since_ms == 999);
    CHECK(q.total_ms == POMO_FOCUS_MS);
    CHECK(q.alarm == false);
}

static void pomo13_decode_rejects_and_leaves_struct(void)
{
    pet_pomo_t src;
    pet_pomo_init(&src, 0);
    src.has_day = true;
    src.day = 5;
    src.done_count = 6;
    src.focus_seconds = 700;
    uint8_t buf[PET_POMO_STATS_LEN];
    CHECK(pet_pomo_encode_stats(buf, sizeof buf, &src) == PET_POMO_STATS_LEN);

    /* 哨兵结构：decode 失败时一个字段都不许被碰 */
    pet_pomo_t g;
    memset(&g, 0, sizeof g);
    g.phase = PET_POMO_BREAK;
    g.since_ms = 1111;
    g.elapsed_ms = 2222;
    g.total_ms = 3333;
    g.done_count = 44;
    g.focus_seconds = 55;
    g.alarm = true;
    g.has_day = true;
    g.day = 66;

    buf[0] = 'X'; /* magic 坏 */
    CHECK(pet_pomo_decode_stats(buf, sizeof buf, &g) == false);
    CHECK(g.phase == PET_POMO_BREAK);
    CHECK(g.since_ms == 1111);
    CHECK(g.elapsed_ms == 2222);
    CHECK(g.total_ms == 3333);
    CHECK(g.done_count == 44);
    CHECK(g.focus_seconds == 55);
    CHECK(g.alarm == true);
    CHECK(g.has_day == true);
    CHECK(g.day == 66);

    buf[0] = 'P';
    buf[1] = 'M';
    buf[2] = 'C';
    buf[3] = '1'; /* magic 修好，只留长度坏 */
    CHECK(pet_pomo_decode_stats(buf, 8, &g) == false);
    CHECK(g.done_count == 44);
    CHECK(g.day == 66);
}

static void pomo13_encode_short_cap_returns_zero(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 0);
    uint8_t buf[16];
    memset(buf, 0xAA, sizeof buf);
    CHECK(pet_pomo_encode_stats(buf, 8, &p) == 0);
    CHECK(buf[0] == 0xAA && buf[15] == 0xAA); /* 短 cap 不写 buf */
}

/* F8（M9 故障注入）：时间回拨不得误判完成/计数；恢复后继续正常推进 */
static void pomo14_rewind_no_false_done(void)
{
    pet_pomo_t p;
    pet_pomo_init(&p, 1000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 1000); /* FOCUS */
    pet_pomo_tick(&p, 11000);                      /* +10 s */
    CHECK(p.elapsed_ms == 10000);
    const uint32_t rewound = 11000u - 30000u;      /* 回拨 30 s（uint32 回绕） */
    pet_pomo_tick(&p, rewound);
    CHECK(p.phase == PET_POMO_FOCUS);
    CHECK(p.done_count == 0);
    CHECK(p.elapsed_ms == 10000);
    pet_pomo_tick(&p, rewound + 1000u);            /* 恢复后 +1 s */
    CHECK(p.elapsed_ms == 11000);
    CHECK(p.phase == PET_POMO_FOCUS);
}

int main(void)
{
    RUN(pomo01_init_defaults);
    RUN(pomo02_short_starts_focus);
    RUN(pomo03_pause_resume);
    RUN(pomo04_focus_completes);
    RUN(pomo05_boundary_exact);
    RUN(pomo06_done_is_sticky);
    RUN(pomo07_focus_then_break_then_idle);
    RUN(pomo08_abandon_focus);
    RUN(pomo09_alarm_take_once);
    RUN(pomo10_daily_reset);
    RUN(pomo11_long_in_idle_noop);
    RUN(pomo12_abandon_from_paused);
    RUN(pomo13_encode_layout_is_le_with_magic);
    RUN(pomo13_roundtrip_restores_stats_only);
    RUN(pomo13_decode_rejects_and_leaves_struct);
    RUN(pomo14_rewind_no_false_done);
    RUN(pomo13_encode_short_cap_returns_zero);
    REPORT();
}
