# M4 事件入口与 OpenCode 接入 契约清单

> 状态：**增量 1（设备侧解析）+ 增量 2a（主机侧入口核心）已冻结（2026-10-16）**；增量 2b（传输 / OpenCode 真源）**阻塞**；最迟冻结：**M4 开工前**
> 依据：`requirements/R11/R12`、`Q12`（先只做 OpenCode）、`Q13`（**入口放主机层**）；`M2` 的 `done` 边沿
> 证据来源：`second-state/vibebuddy` → `docs/protocol.md`（**文档级**，未在本项目实测）

## 1. 冻结项

| # | 冻结项 | 需要产出的值 | 关联测试 | 状态 |
| --- | --- | --- | --- | --- |
| 4.1 | 本机入口承载 | `127.0.0.1:8787`（提案，`PET_EVENT_PORT` 可覆盖）；**仅 loopback** | E2E-02 | 🟡 增量 2a 提案（端口待批准） |
| 4.2 | 入口协议 | HTTP `POST /v1/events`，`content-type: application/json` | E2E-02 | ✅ 上游证据（借用语义） |
| 4.3 | 事件 schema | 见 §3 | E2E-02 | 🟡 部分有证据 |
| 4.4 | 鉴权 | **仅 loopback**（已实现）；是否需要 token | SEC-01 | 🟡 增量 2a 实现 loopback；token 待定 |
| 4.5 | OpenCode → `pet_event_t` 映射 | 见 §4 | CARD-02 | ⏳ 待产出（依赖旧项目 M10 快照） |
| 4.6 | `done` 边沿来源 | 快照下降沿 vs 旧项目事件 | ST-02 | ⏳ 待产出（B01） |
| 4.7 | 非法输入处理 | 不崩、回错误、不广播 | E2E-03 | ✅ 增量 2a（见 §8.3） |

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
- （**增量 1**）不做 `tasks`/`stats` 数组、不做主机侧 HTTP 入口与传输。

## 8. 冻结（2026-10-16，M4 增量 2a：**主机侧事件入口核心**）

> 范围：**主机侧**（`Q13`）事件入口的**核心逻辑**：收事件 → 校验/映射 → 产出**设备事件线**（§6.1）+ HTTP 错误处理。
> **语言/运行时：Python 3（仅 stdlib，无 pip）**。理由：入口跑在 Windows（用户机有 Python 3.14）、HTTP 用 stdlib `http.server`、仓库已有 Python 工具；将来接 BLE/串口更易。**这是本节点新增的工程决定**（契约此前只定"入口在主机层"，未定语言）。
> **本增量不做**：真实 socket→设备的**传输**（固件无收件通道 + 本机无 ESP-IDF）、**OpenCode 真源映射**（旧项目不在本机）——两者登记为**阻塞，不伪造**；`M4` 节点因此**不算完成**。

### 8.1 接口（4.1/4.2）

- 承载：`127.0.0.1:<EVENT_INGEST_PORT>`，**仅 loopback**（4.4；token 仍待定）。
- 端点：`POST /v1/events`，`Content-Type: application/json`（允许 `; charset=…`）。
- 其它方法/路径 → `405`/`404`；非 JSON content-type → `415`。

### 8.2 输入 → 设备线（§6.1）映射

- 输入可为**简式**（顶层 `version`/`event`/`title`/`project`/`elapsed_s`）或**富式**（含 `tasks` 数组；`stats` **忽略**，属 M8）。
- **优先级**：有 `tasks` 时取 `tasks[0]` 的 `title`/`project`/`elapsed_s`；否则取顶层。
- 产出：**一行** NDJSON（键序 `version`,`event`[,`title`][,`project`][,`elapsed_s`] + `\n`）；`title`/`project` 截断到 **63**（`TITLE_MAX-1`/`PROJECT_MAX-1`）。

### 8.3 校验与错误码（4.7：不崩、回错、**不广播**）

| 情形 | 码 | HTTP |
| --- | --- | --- |
| 合法 | `OK` | 200 |
| 非法 JSON / 顶层非对象 | `JSON` | 400 |
| `version` 缺失或 ≠1 | `VERSION` | 400 |
| `event` 缺失或非字符串 | `EVENT` | 400 |
| `event` 不在已知集合 | `UNKNOWN_EVENT` | 400 |
| `title`/`project` 非字符串，或 `elapsed_s` 非**非负整数** | `TYPE` | 400 |
| body 超 `MAX_BODY`（512） | `TOO_LONG` | 413 |
| 方法/路径/content-type 不符 | `METHOD`/`NOT_FOUND`/`CONTENT_TYPE` | 405/404/415 |

- 已知事件集合 = §6.2 的 7 个（`task.start`/`agent.input_required`/`task.done`/`task.error`/`agent.blocked`/`agent.idle`/`device.heartbeat`）。

### 8.4 传输注入点

- 服务器持有可注入 **sink**（`callable(line)->None`），默认=日志；将来接 BLE/串口时替换。**本增量不实装传输**。

### 8.5 参数（提案值，**待批准**）

| 符号 | 值 | 依据 |
| --- | --- | --- |
| `EVENT_INGEST_PORT` | `8787` | 本项目提案（4.1 端口待定；可用环境变量 `PET_EVENT_PORT` 覆盖） |
| `MAX_BODY` | 512 | 对齐设备 `PET_LINE_MAX` |
| `TITLE_MAX`/`PROJECT_MAX` | 64 | 对齐 `pet_types.h` |

### 8.6 明确不做（增量 2a）

- 不做真实传输（BLE/串口）；不做 **OpenCode 映射**；不做 `stats` 轮播；不做鉴权 token；不做多卡。

## 9. 冻结（2026-10-17，M4 增量 2b：**USB-Serial-JTAG 传输**）

> **状态：✅ 已实现并真机端到端通过（2026-10-17）** —— 设备侧 `pet_rx`/`pet_linebuf`；主机侧 `pet_transport`/`pet_sender`。真机：脚本发事件 → 设备状态/卡/语音均生效，见 `evidence/M4/2026-10-17-传输端到端-H.md`。**OpenCode 映射仍不做**（`R11`）。

> **传输决定**：本增量选 **USB-Serial-JTAG CDC（有线）**，**不用 BLE(S3P)**。
> **理由**：旧项目（BLE 客户端 + `state_host`）**不在本机**；固件**无 BLE 栈/收件通道**；无 UART 接线。设备已在 `COM5`，控制台即走 USB-Serial-JTAG → **唯一现在可行**的传输。
> **这是对 §2 架构图（原写 BLE）的修订**，登记于此；**BLE/WiFi 角色仍属 `Q17`（未决）**，将来若要 BLE 另开增量/ADR。
> 目标：让**任意本机脚本发的真事件**到达设备并驱动状态/卡/播报 → 解 `R12`/`E2E-02` **端到端** + 剩余 H。

### 9.1 线协议

- 复用 §6.1：UTF-8 NDJSON，一行一个对象，`\n` 结束，行长 ≤ `PET_LINE_MAX`(512)。**超长 → 丢弃该行**（记 warn）。
- 物理：USB-Serial-JTAG CDC（`COM5`）；baud 名义 115200（CDC 忽略）。

### 9.2 设备侧（`pet_rx`）

- 新模块 `pet_rx.c/.h`：一个任务读 USB-Serial-JTAG 字节 → **行缓冲** → 完整行 → `pet_link_parse` → `pet_state_apply`；`has_card` → `pet_state_set_card`。
- 解析失败/超长：**记 warn、丢弃、不改状态、不崩**。
- 行缓冲做成**纯逻辑**（`pet_linebuf`，可 host 单测）；接线：`pet_rx_start(struct pet_state *s)`。
- 行兼容 CRLF（剥尾部 `\r`）。

### 9.3 主机侧

- `host/pet_transport.py`：`ensure_line(obj_or_str) -> bytes`（保证单个结尾 `\n`）＋ `SerialSink(port, baud)`（写行；可用注入的 `serial` 以便无硬件单测）。
- `host/pet_sender.py` CLI：从 argv 或 stdin 读 JSON，写到 `--port`（默认 `COM5`）。
- **OpenCode 映射仍不做**（旧项目不在；`Q12`）。

### 9.4 参数（提案值，待批准）

| 符号 | 值 | 依据 |
| --- | --- | --- |
| `PET_LINE_MAX` | 512 | 已冻 §6.6 |
| `PET_RX_BAUD` | 115200 | USB-CDC 名义值 |
| `PET_EVENT_PORT` | 8787 | §8.5（HTTP 入口，仍有效） |

### 9.5 明确不做（增量 2b）

- 不做 BLE/WiFi 传输（`Q17`）；不做 **OpenCode 映射**；不做多卡/`stats`；不做鉴权 token。
