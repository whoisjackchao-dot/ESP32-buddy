# M4 事件入口与 OpenCode 接入 契约清单

> 状态：**清单就绪**；最迟冻结：**M4 开工前**
> 依据：`requirements/R11/R12`、`Q12`（先只做 OpenCode）、`Q13`（**入口放主机层**）；`M2` 的 `done` 边沿
> 证据来源：`second-state/vibebuddy` → `docs/protocol.md`（**文档级**，未在本项目实测）

## 1. 冻结项

| # | 冻结项 | 需要产出的值 | 关联测试 | 状态 |
| --- | --- | --- | --- | --- |
| 4.1 | 本机入口承载 | Windows 主机上 `127.0.0.1:<port>`（端口号待定） | E2E-02 | ⏳ 待产出 |
| 4.2 | 入口协议 | HTTP `POST /v1/events`，`content-type: application/json` | E2E-02 | ✅ 上游证据（借用语义） |
| 4.3 | 事件 schema | 见 §3 | E2E-02 | 🟡 部分有证据 |
| 4.4 | 鉴权 | 仅 loopback；是否需要 token | SEC-01 | ⏳ 待产出 |
| 4.5 | OpenCode → `pet_event_t` 映射 | 见 §4 | CARD-02 | ⏳ 待产出（依赖旧项目 M10 快照） |
| 4.6 | `done` 边沿来源 | 快照下降沿 vs 旧项目事件 | ST-02 | ⏳ 待产出（B01） |
| 4.7 | 非法输入处理 | 不崩、回错误、不广播 | E2E-03 | ⏳ 待产出 |

## 2. 架构（Q13 已定）

```
任意本机程序/脚本 ──HTTP──► Windows 主机的事件入口 ──BLE(S3P)──► 设备 pet_state
OpenCode(M10 快照) ─────────┘
```
- **入口在主机层**，设备保持简单；设备只收已有的 S3P/BLE。
- 与旧项目复用：主机侧已有 BLE 客户端与 `state_host`，本节点是**在其上加一个 HTTP 入口 + 映射**。

## 3. 事件 schema（借用上游语义，标证据）

上游最小体（文档级证据）：

```json
{"version":1,"event":"task.done","title":"Hello"}
```
带卡与统计：
```json
{"version":1,"event":"task.start","title":"GAMMA",
 "tasks":[{"title":"GAMMA","status":"working","elapsed_s":75},{"title":"BETA","status":"input_required","elapsed_s":900}],
 "stats":["7 DONE","4 ASKS","1H23 BUSY"]}
```

| 事件 | 语义 | 关联状态 |
| --- | --- | --- |
| `task.start` | 开始 | WORKING |
| `agent.input_required` | 等待确认 | INPUT_REQUIRED |
| `task.done` | 完成（一次性） | DONE |
| `task.error` / `agent.blocked` | 失败 | FAILED |
| `agent.idle` | 空闲 | IDLE |
| `device.heartbeat` | 链路保活 | 不改变状态 |

- `status` 取值：`working / input_required / done / failed`。
- **本项目范围**：M4 只消费**单卡**（`tasks[0]`）与 `stats`；多卡留 M8。

## 4. OpenCode 映射（待产出）

| 旧项目快照字段 | 本项目事件/状态 | 备注 |
| --- | --- | --- |
| `busy` / `retry` | `task.start` → WORKING | `retry` 是否算 WORKING 待定 |
| `waiting`（pending permission/question） | `agent.input_required` | ✅ |
| `error` | `task.error` → FAILED | ✅ |
| `idle` | `agent.idle` → IDLE | **`done` 需由下降沿合成**（B01） |
| 快照超时（15s） | 无事件 → OFFLINE | 阈值见 M2 |

## 6. 冻结（2026-10-16，M4 增量 1：**device 侧事件线格式**）

> 本增量只冻结 **设备侧"一行事件 → `pet_event_t`"的解析与映射**（纯逻辑、可 host 测）。
> 主机侧 HTTP 入口（4.1/4.2/4.4）、传输（BLE/UART）、OpenCode 映射（4.5）留到增量 2。

### 6.1 线格式（NDJSON）

- **一行一个 JSON 对象**，UTF-8，以 `\n` 结尾；
- 行长上限 **`PET_LINE_MAX = 512`** 字节，超长 → `TOO_LONG`（丢弃）；
- 键：
  - **必需**：`version`（数字，必须 `== 1`）、`event`（字符串）
  - **可选**：`title`（字符串）、`project`（字符串）、`elapsed_s`（**非负整数**）
- **未知键忽略**；**不支持嵌套对象与数组**（`tasks`/`stats` 属 M8）；
- 类型不符 / 缺必需 / JSON 非法 → 返回错误，**不产生事件、不崩溃**；
- 字符串支持转义 `\" \\ \/ \n \t`；`title`/`project` 超出 `PET_TITLE_MAX-1`（=63）**截断**。

### 6.2 事件名 → 事件种类

| `event` | `pet_event_kind_t` | 说明 |
| --- | --- | --- |
| `task.start` | `PET_EV_TASK_START` | → WORKING |
| `agent.input_required` | `PET_EV_AGENT_INPUT_REQUIRED` | → INPUT_REQUIRED |
| `task.done` | `PET_EV_TASK_DONE` | → DONE（显式，不去抖） |
| `task.error` / `agent.blocked` | `PET_EV_TASK_ERROR` | → FAILED |
| `agent.idle` | `PET_EV_AGENT_IDLE` | → IDLE（**边沿去抖在 `pet_state` 内**） |
| `device.heartbeat` | `PET_EV_ANNOUNCEMENT` | **只刷新链路计时**，不改状态 |
| 其它 | — | `UNKNOWN_EVENT` |

### 6.3 卡片

- 该行出现 `title` / `project` / `elapsed_s` **任一** → 产生**卡片更新**；
- `card.state` 取**该事件映射后的新状态**（如 `task.start` → `WORKING`）；
- 卡片**保持**，直到被新行替换；**清空属 M8**。

### 6.4 错误码（只用这些）

`OK` / `TOO_LONG` / `JSON` / `VERSION` / `EVENT` / `TYPE` / `UNKNOWN_EVENT`

### 6.5 状态归属（`pet_state`）

- 新增 `pet_state_set_card(s, card|NULL)`：存/清卡片；`pet_state_view` 把它填进 `pet_view.card` 并置 `has_card`。

### 6.6 参数（提案值，**待批准**）

| 符号 | 值 |
| --- | --- |
| `PET_LINE_MAX` | 512 |

## 7. 明确不做

- 不做设备直连入口（Q13 选了主机层）。
- 不做除 OpenCode 外的 Agent（Q12）。
- 不做多卡栈（M8）。
- 不提供自动提交/审批/Shell 接口。
- 本增量**不做** `tasks`/`stats` 数组、不做主机侧 HTTP 入口与传输。
