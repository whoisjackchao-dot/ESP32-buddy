# evidence/M4 —— OpenCode 真源可行性 spike（R11）

> 日期：**2026-10-17**；类别：**spike（限范围调研）**；关联 `R11`/`E2E-01`、`Q12`、`B09`
> 授权：用户在批准计划时授权"先做限范围 spike（读 OpenCode 状态：API/日志/插件哪种可行）"。

## 1. 假设

能从本机拿到 OpenCode 的状态（busy / waiting / error / idle），从而把"真源"映射成设备事件。

## 2. 事实（本机实测）

| 项 | 观测 |
| --- | --- |
| 版本/位置 | OpenCode **1.18.34**，`~/.opencode/bin/opencode` |
| 数据 | **SQLite** `~/.local/share/opencode/opencode.db`（≈154 MB + WAL），可**只读并发**打开 |
| 本地服务 | `127.0.0.1:35249`（pid=opencode）→ `GET /` 返回 `{"skills":[...]}`（**插件/技能服务**，非核心状态 API；`/session` `/config` 等 404） |
| `message` 表 | 列 `session_id/time_created/time_updated/data(JSON)`；`data` 含 `role`、`time.completed`、`error` |
| `part` 表 | `data.type ∈ {tool, step-start, step-finish, reasoning, text}`；`tool.state.status ∈ {running, completed, error}` |
| `event` 表 | `type ∈ {message.updated.1, message.part.updated.1, session.updated.1, session.created.1}`，带 `seq`（**可增量**） |
| `session` 表 | `id/title/directory/time_updated/...`；`permission` 列是**策略**不是状态 |
| `permission` 表 | 存在（id/action/resource），当前 **0 行**；历史 `part` 里**无** permission/question 类型 |
| 日志 | `~/.local/share/opencode/log/opencode.log`：只有 `message=evaluated permission=...`（**自动放行**），无"等待用户"状态 |
| 现场验证 | 最新 assistant `message` **无 `time.completed`** → 正在生成（busy 可判）；历史 949 completed / 2 error |

## 3. 判定（按目标状态）

| 状态 | 可得性 | 依据 |
| --- | --- | --- |
| **busy（WORKING）** | ✅ | 最新 assistant 消息缺 `time.completed`，或存在 `tool.state.status=running` |
| **idle（IDLE）** | ✅ | 最新 assistant 已 `completed` 且无更新的 user 消息 |
| **done（边沿）** | ✅（host 侧） | `busy → idle/completed` 的**边沿**由 host 轮询器记录（对应 `B01`） |
| **error（FAILED）** | ✅ | `message.data.error` 非空，或 tool 状态 `error` |
| **waiting（INPUT_REQUIRED，需要确认）** | ❌ **未验证** | 无持久化证据；`permission` 表疑似承载 pending，但**未观测到**一次真实"询问" |

## 4. 推荐方案

- **P1-2a（可立即做）**：host 侧 **SQLite 只读轮询器**（~1 s），派生 `busy/idle/done/error` → 复用现有传输发设备线（`pet_sender` 同款）。取 **`time_updated` 最新的顶层 session** 作为"当前会话"。
- **P1-2b（需一次真实提示）**：用一次**真实 ask/question 权限提示**观测 `permission` 表 / 事件，确认 `waiting` 形态后再接 `INPUT_REQUIRED`。

## 5. 风险

| 风险 | 说明 |
| --- | --- |
| **内部 schema** | `opencode.db` 是**内部实现**，随版本可能变（1.18.34 有效，升级需复验） |
| 会话选择 | 多 session/多项目时"当前会话"的判定（取最新 `time_updated`）可能不符直觉 |
| WAL 并发 | 只读打开可行（已实测），但需 `mode=ro` 且容错锁 |
| done 边沿 | 必须由 host 侧记录前后状态（设备侧只剩电平） |
| waiting | 未验证 → `INPUT_REQUIRED` 暂不能承诺 |

## 6. 终止条件 / 验收（P1-2a）

- 真实跑一个 OpenCode 任务：设备进入 **WORKING**；结束转 **DONE**（+语音）；出错转 **FAILED**；`E2E-01` 记为实测。
