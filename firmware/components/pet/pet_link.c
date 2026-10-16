/* M4 增量 1：device 侧事件线解析（NDJSON → 事件 + 卡片）。
 * 契约：contracts/M4-事件入口与OpenCode接入契约清单.md §6。
 * 只支持：单层 JSON 对象 + 字符串/非负整数/true/false/null；
 * 嵌套对象与数组**不支持**（返回 JSON 错误；tasks/stats 属 M8）。 */
#include <string.h>

#include "pet_link.h"

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

/* 解析 JSON 字符串；截断到 cap-1 并存 NUL。返回字符串之后的位置，失败返回 NULL。 */
static const char *parse_string(const char *p, const char *end, char *out, size_t cap)
{
    if (p >= end || *p != '"') {
        return NULL;
    }
    p++;
    size_t n = 0;
    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    while (p < end) {
        char c = *p;
        if (c == '"') {
            if (out != NULL && cap > 0) {
                out[(n < cap - 1) ? n : cap - 1] = '\0';
            }
            return p + 1;
        }
        if (c == '\\') {
            p++;
            if (p >= end) {
                return NULL;
            }
            switch (*p) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case 'r':  c = '\r'; break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            default:   return NULL; /* 不支持 \u 等 */
            }
        } else if ((unsigned char)c < 0x20u) {
            return NULL; /* 裸控制字符非法 */
        }
        if (out != NULL && cap > 0 && n < cap - 1) {
            out[n] = c;
        }
        n++;
        p++;
    }
    return NULL; /* 未闭合 */
}

/* 解析非负整数。失败返回 NULL（调用方按字段决定错误码）。 */
static const char *parse_uint(const char *p, const char *end, long *out)
{
    if (p >= end || *p < '0' || *p > '9') {
        return NULL;
    }
    long v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 1000000000L) {
            return NULL;
        }
        p++;
    }
    *out = v;
    return p;
}

/* 跳过**仅** 字符串/数字/true/false/null 的值；其它（对象/数组）→ NULL */
static const char *skip_value(const char *p, const char *end)
{
    if (p >= end) {
        return NULL;
    }
    if (*p == '"') {
        return parse_string(p, end, NULL, 0);
    }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        if (*p == '-') {
            p++;
        }
        if (p >= end || *p < '0' || *p > '9') {
            return NULL;
        }
        while (p < end && *p >= '0' && *p <= '9') {
            p++;
        }
        /* 小数/指数：仅为跳过，简单接受 */
        if (p < end && *p == '.') {
            p++;
            while (p < end && *p >= '0' && *p <= '9') {
                p++;
            }
        }
        return p;
    }
    if (end - p >= 4 && memcmp(p, "true", 4) == 0) {
        return p + 4;
    }
    if (end - p >= 5 && memcmp(p, "false", 5) == 0) {
        return p + 5;
    }
    if (end - p >= 4 && memcmp(p, "null", 4) == 0) {
        return p + 4;
    }
    return NULL;
}

static bool map_event(const char *name, pet_event_kind_t *kind, pet_state_t *state)
{
    if (strcmp(name, "task.start") == 0) {
        *kind = PET_EV_TASK_START;
        *state = PET_STATE_WORKING;
    } else if (strcmp(name, "agent.input_required") == 0) {
        *kind = PET_EV_AGENT_INPUT_REQUIRED;
        *state = PET_STATE_INPUT_REQUIRED;
    } else if (strcmp(name, "task.done") == 0) {
        *kind = PET_EV_TASK_DONE;
        *state = PET_STATE_DONE;
    } else if (strcmp(name, "task.error") == 0 || strcmp(name, "agent.blocked") == 0) {
        *kind = PET_EV_TASK_ERROR;
        *state = PET_STATE_FAILED;
    } else if (strcmp(name, "agent.idle") == 0) {
        *kind = PET_EV_AGENT_IDLE;
        *state = PET_STATE_IDLE;
    } else if (strcmp(name, "device.heartbeat") == 0) {
        *kind = PET_EV_ANNOUNCEMENT; /* 只刷新链路 */
        *state = PET_STATE_IDLE;
    } else {
        return false;
    }
    return true;
}

pet_link_status_t pet_link_parse(const char *line, size_t len, pet_link_msg_t *out)
{
    if (out == NULL) {
        return PET_LINK_ERR_JSON;
    }
    memset(out, 0, sizeof *out);
    if (line == NULL) {
        return PET_LINK_ERR_JSON;
    }
    if (len > PET_LINE_MAX) {
        return PET_LINK_ERR_TOO_LONG;
    }

    const char *p = line;
    const char *end = line + len;

    p = skip_ws(p, end);
    if (p >= end || *p != '{') {
        return PET_LINK_ERR_JSON;
    }
    p++;

    long version = -1;
    bool has_version = false;
    bool has_event = false;
    char event[48] = {0};
    char title[PET_TITLE_MAX] = {0};
    char project[PET_PROJECT_MAX] = {0};
    bool has_title = false;
    bool has_project = false;
    bool has_elapsed = false;
    long elapsed = 0;

    p = skip_ws(p, end);
    if (p < end && *p == '}') {
        p++;
    } else {
        for (;;) {
            p = skip_ws(p, end);
            char key[32];
            const char *q = parse_string(p, end, key, sizeof key);
            if (q == NULL) {
                return PET_LINK_ERR_JSON;
            }
            p = skip_ws(q, end);
            if (p >= end || *p != ':') {
                return PET_LINK_ERR_JSON;
            }
            p = skip_ws(p + 1, end);

            if (strcmp(key, "version") == 0) {
                const char *vend = parse_uint(p, end, &version);
                if (vend == NULL) {
                    return PET_LINK_ERR_VERSION;
                }
                has_version = true;
                p = vend;
            } else if (strcmp(key, "event") == 0) {
                q = parse_string(p, end, event, sizeof event);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE;
                }
                has_event = true;
                p = q;
            } else if (strcmp(key, "title") == 0) {
                q = parse_string(p, end, title, sizeof title);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE;
                }
                has_title = true;
                p = q;
            } else if (strcmp(key, "project") == 0) {
                q = parse_string(p, end, project, sizeof project);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE;
                }
                has_project = true;
                p = q;
            } else if (strcmp(key, "elapsed_s") == 0) {
                q = parse_uint(p, end, &elapsed);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE; /* 负数/字符串/非数字 */
                }
                has_elapsed = true;
                p = q;
            } else {
                q = skip_value(p, end);
                if (q == NULL) {
                    return PET_LINK_ERR_JSON; /* 未知键的嵌套/数组不支持 */
                }
                p = q;
            }

            p = skip_ws(p, end);
            if (p < end && *p == ',') {
                p++;
                continue;
            }
            if (p < end && *p == '}') {
                p++;
                break;
            }
            return PET_LINK_ERR_JSON;
        }
    }

    p = skip_ws(p, end);
    if (p != end) {
        return PET_LINK_ERR_JSON; /* 尾部多余内容 */
    }
    if (!has_version || version != 1) {
        return PET_LINK_ERR_VERSION;
    }
    if (!has_event) {
        return PET_LINK_ERR_EVENT;
    }

    pet_event_kind_t kind;
    pet_state_t mapped;
    if (!map_event(event, &kind, &mapped)) {
        return PET_LINK_ERR_UNKNOWN_EVENT;
    }

    out->kind = kind;
    if (has_title || has_project || has_elapsed) {
        out->has_card = true;
        if (has_title) {
            memcpy(out->card.title, title, sizeof out->card.title);
        }
        if (has_project) {
            memcpy(out->card.project, project, sizeof out->card.project);
        }
        if (has_elapsed) {
            out->card.elapsed_s = (int32_t)elapsed;
        }
        out->card.state = mapped;
    }
    return PET_LINK_OK;
}
