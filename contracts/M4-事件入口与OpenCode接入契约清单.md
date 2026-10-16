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

## 5. 明确不做

- 不做设备直连入口（Q13 选了主机层）。
- 不做除 OpenCode 外的 Agent（Q12）。
- 不做多卡栈（M8）。
- 不提供自动提交/审批/Shell 接口。
