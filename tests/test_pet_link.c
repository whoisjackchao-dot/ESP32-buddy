/* M4 T 类：device 侧事件线解析（NDJSON → 事件 + 卡片）。
 * 契约：contracts/M4-事件入口与OpenCode接入契约清单.md §6；
 *       contracts/M8-休闲与多卡栈契约清单.md §8.1（tasks[] 多卡，M8 扩展）。 */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "pet_link.h"

static pet_link_msg_t msg;

static pet_link_status_t parse(const char *line)
{
    memset(&msg, 0, sizeof msg);
    return pet_link_parse(line, strlen(line), &msg);
}

/* ---- 合法事件 ---- */

static void task_start_carries_card(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"title\":\"BUILD\"," 
                "\"project\":\"ESP32-buddy\",\"elapsed_s\":75}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_TASK_START);
    CHECK(msg.has_card);
    CHECK(strcmp(msg.card.title, "BUILD") == 0);
    CHECK(strcmp(msg.card.project, "ESP32-buddy") == 0);
    CHECK(msg.card.elapsed_s == 75);
    CHECK(msg.card.state == PET_STATE_WORKING); /* 卡片状态 = 该事件映射的新状态 */
}

static void all_event_names_map(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"agent.input_required\"}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_AGENT_INPUT_REQUIRED);
    CHECK(!msg.has_card);

    CHECK(parse("{\"version\":1,\"event\":\"task.done\"}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_TASK_DONE);

    CHECK(parse("{\"version\":1,\"event\":\"task.error\"}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_TASK_ERROR);

    CHECK(parse("{\"version\":1,\"event\":\"agent.blocked\"}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_TASK_ERROR);

    CHECK(parse("{\"version\":1,\"event\":\"agent.idle\"}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_AGENT_IDLE);

    CHECK(parse("{\"version\":1,\"event\":\"device.heartbeat\"}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_ANNOUNCEMENT); /* 只刷新链路，不改状态 */
    CHECK(!msg.has_card);
}

static void card_state_follows_event(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.done\",\"title\":\"X\"}") == PET_LINK_OK);
    CHECK(msg.has_card);
    CHECK(msg.card.state == PET_STATE_DONE);

    CHECK(parse("{\"version\":1,\"event\":\"task.error\",\"title\":\"X\"}") == PET_LINK_OK);
    CHECK(msg.card.state == PET_STATE_FAILED);

    CHECK(parse("{\"version\":1,\"event\":\"agent.input_required\",\"title\":\"X\"}") == PET_LINK_OK);
    CHECK(msg.card.state == PET_STATE_INPUT_REQUIRED);
}

static void tolerates_whitespace_unknown_keys_and_newline(void)
{
    CHECK(parse("{ \"version\" : 1 , \"event\" : \"agent.idle\" , \"extra\" : \"ignored\" }\n")
          == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_AGENT_IDLE);
    CHECK(!msg.has_card);
}

static void truncates_overlong_title(void)
{
    char line[1024];
    char big[200];
    memset(big, 'W', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    snprintf(line, sizeof line, "{\"version\":1,\"event\":\"task.start\",\"title\":\"%s\"}", big);
    CHECK(parse(line) == PET_LINK_OK);
    CHECK(msg.has_card);
    CHECK(strlen(msg.card.title) == PET_TITLE_MAX - 1); /* 63 */
}

/* ---- 非法输入：必须拒且不崩 ---- */

static void rejects_malformed_json(void)
{
    CHECK(parse("not json") == PET_LINK_ERR_JSON);
    CHECK(parse("{\"version\":1,\"event\":\"agent.idle\"") == PET_LINK_ERR_JSON); /* 缺右括号 */
    CHECK(parse("[]") == PET_LINK_ERR_JSON);                                     /* 不是对象 */
    CHECK(parse("") == PET_LINK_ERR_JSON);
}

static void rejects_bad_version(void)
{
    CHECK(parse("{\"event\":\"agent.idle\"}") == PET_LINK_ERR_VERSION); /* 缺 version */
    CHECK(parse("{\"version\":2,\"event\":\"agent.idle\"}") == PET_LINK_ERR_VERSION);
    CHECK(parse("{\"version\":\"1\",\"event\":\"agent.idle\"}") == PET_LINK_ERR_VERSION);
}

static void rejects_missing_event(void)
{
    CHECK(parse("{\"version\":1}") == PET_LINK_ERR_EVENT);
}

static void rejects_unknown_event(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.explode\"}") == PET_LINK_ERR_UNKNOWN_EVENT);
}

static void rejects_wrong_types(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"title\":123}") == PET_LINK_ERR_TYPE);
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"elapsed_s\":-5}") == PET_LINK_ERR_TYPE);
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"elapsed_s\":\"9\"}") == PET_LINK_ERR_TYPE);
}

static void rejects_overlong_line(void)
{
    static char big[PET_LINE_MAX + 64];
    memset(big, ' ', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    CHECK(pet_link_parse(big, sizeof big - 1, &msg) == PET_LINK_ERR_TOO_LONG);
}

/* ---- M8 §8.1：tasks[] 多卡 ---- */

static void tasks_two_cards_parse_with_status_mapping(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                "{\"title\":\"GAMMA\",\"project\":\"P1\",\"elapsed_s\":75,\"status\":\"working\"},"
                "{\"title\":\"BETA\",\"elapsed_s\":900,\"status\":\"input_required\"}]}")
          == PET_LINK_OK);
    CHECK(msg.card_count == 2);
    CHECK(strcmp(msg.cards[0].title, "GAMMA") == 0);
    CHECK(strcmp(msg.cards[0].project, "P1") == 0);
    CHECK(msg.cards[0].elapsed_s == 75);
    CHECK(msg.cards[0].state == PET_STATE_WORKING);
    CHECK(strcmp(msg.cards[1].title, "BETA") == 0);
    CHECK(msg.cards[1].project[0] == '\0'); /* 缺 project → 空串 */
    CHECK(msg.cards[1].elapsed_s == 900);
    CHECK(msg.cards[1].state == PET_STATE_INPUT_REQUIRED);
}

static void tasks_three_cards_missing_and_unknown_status_default_working(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                "{\"title\":\"A\",\"status\":\"done\"},"
                "{\"title\":\"B\"},"
                "{\"title\":\"C\",\"status\":\"exploded\"}]}") == PET_LINK_OK);
    CHECK(msg.card_count == 3);
    CHECK(msg.cards[0].state == PET_STATE_DONE);
    CHECK(msg.cards[1].state == PET_STATE_WORKING); /* 缺 status → working */
    CHECK(msg.cards[1].elapsed_s == 0);
    CHECK(msg.cards[2].state == PET_STATE_WORKING); /* 未知 status → working */
}

static void tasks_failed_maps_and_four_items_capped_at_three(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                "{\"title\":\"A\",\"status\":\"failed\"},"
                "{\"title\":\"B\"},{\"title\":\"C\"},{\"title\":\"D\"}]}")
          == PET_LINK_OK);
    CHECK(msg.card_count == 3); /* 只取前 3 项 */
    CHECK(msg.cards[0].state == PET_STATE_FAILED);
    CHECK(strcmp(msg.cards[0].title, "A") == 0);
    CHECK(strcmp(msg.cards[1].title, "B") == 0);
    CHECK(strcmp(msg.cards[2].title, "C") == 0);
}

static void tasks_non_list_ignored_and_single_card_still_works(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"title\":\"SOLO\","
                "\"elapsed_s\":5,\"tasks\":7}") == PET_LINK_OK);
    CHECK(msg.card_count == 0);
    CHECK(msg.has_card); /* 顶层单卡字段不受影响 */
    CHECK(strcmp(msg.card.title, "SOLO") == 0);
    CHECK(msg.card.elapsed_s == 5);

    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"title\":\"SOLO\","
                "\"tasks\":{\"x\":1}}") == PET_LINK_OK); /* 对象 → 忽略该键 */
    CHECK(msg.card_count == 0);
    CHECK(msg.has_card);
    CHECK(msg.cards[0].title[0] == '\0');
}

static void tasks_non_object_items_skipped(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                "\"junk\",{\"title\":\"A\"},42,{\"title\":\"B\"},[1,2]]}")
          == PET_LINK_OK);
    CHECK(msg.card_count == 2); /* 非对象项不占位 */
    CHECK(strcmp(msg.cards[0].title, "A") == 0);
    CHECK(strcmp(msg.cards[1].title, "B") == 0);
}

static void tasks_item_with_type_error_skipped(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                "{\"title\":123},{\"title\":\"OK\"},{\"elapsed_s\":\"x\"}]}")
          == PET_LINK_OK);
    CHECK(msg.card_count == 1); /* 类型不符的项忽略 */
    CHECK(strcmp(msg.cards[0].title, "OK") == 0);
}

static void tasks_and_top_level_card_coexist(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"title\":\"TOP\","
                "\"tasks\":[{\"title\":\"T1\"},{\"title\":\"T2\"}]}") == PET_LINK_OK);
    CHECK(msg.has_card);
    CHECK(strcmp(msg.card.title, "TOP") == 0);
    CHECK(msg.card_count == 2);
    CHECK(strcmp(msg.cards[0].title, "T1") == 0);
    CHECK(strcmp(msg.cards[1].title, "T2") == 0);
}

static void unknown_key_array_value_no_longer_json_error(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"agent.idle\","
                "\"stats\":[\"7 DONE\",\"1H23 BUSY\"]}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_AGENT_IDLE);
    CHECK(msg.card_count == 0);
}

static void nested_but_bounded_unknown_value_still_ok(void)
{
    /* 嵌套深度 ≤ SKIP_MAX_DEPTH(8) 的未知键值仍正常跳过（回归锁：深度帽不收紧） */
    CHECK(parse("{\"version\":1,\"event\":\"agent.idle\","
                "\"stats\":[[[[[\"deep\"]]]]]}") == PET_LINK_OK);
    CHECK(msg.kind == PET_EV_AGENT_IDLE);
}

static void malformed_tasks_array_still_json_error(void)
{
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"tasks\":[{\"title\":\"A\"}")
          == PET_LINK_ERR_JSON); /* 数组语法坏 → 仍 JSON 错 */
    CHECK(parse("{\"version\":1,\"event\":\"task.start\",\"tasks\":[}]")
          == PET_LINK_ERR_JSON);
}

int main(void)
{
    RUN(task_start_carries_card);
    RUN(all_event_names_map);
    RUN(card_state_follows_event);
    RUN(tolerates_whitespace_unknown_keys_and_newline);
    RUN(truncates_overlong_title);
    RUN(rejects_malformed_json);
    RUN(rejects_bad_version);
    RUN(rejects_missing_event);
    RUN(rejects_unknown_event);
    RUN(rejects_wrong_types);
    RUN(rejects_overlong_line);
    RUN(tasks_two_cards_parse_with_status_mapping);
    RUN(tasks_three_cards_missing_and_unknown_status_default_working);
    RUN(tasks_failed_maps_and_four_items_capped_at_three);
    RUN(tasks_non_list_ignored_and_single_card_still_works);
    RUN(tasks_non_object_items_skipped);
    RUN(tasks_item_with_type_error_skipped);
    RUN(tasks_and_top_level_card_coexist);
    RUN(unknown_key_array_value_no_longer_json_error);
    RUN(nested_but_bounded_unknown_value_still_ok);
    RUN(malformed_tasks_array_still_json_error);
    REPORT();
}
