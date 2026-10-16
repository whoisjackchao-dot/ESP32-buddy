/* M4 T 类：device 侧事件线解析（NDJSON → 事件 + 卡片）。
 * 契约：contracts/M4-事件入口与OpenCode接入契约清单.md §6。 */
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
    REPORT();
}
