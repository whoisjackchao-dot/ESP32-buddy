# evidence/M4 —— OpenCode 真源 · 端到端（H 类）

> 日期：**2026-10-17**；类别：**H（真机端到端）**；关联 `R11`/`E2E-01`、`Q12`、`M4 §9`
> 结论范围：**已实测**——真实 OpenCode 会话的 busy/idle 被主机轮询并驱动设备（WORKING → DONE + 语音）。**未**覆盖 `waiting`（需真实权限提示）。

## 0. 架构（跨 WSL/Windows）

- OpenCode 数据库在 **WSL**（`~/.local/share/opencode/opencode.db`）；设备在 **Windows COM5**。
- **UNC 直读失败**（`database is locked` / 无法打开）——SQLite 锁在 9P 上不可靠，因此轮询器必须在 WSL 运行。
- 采用**两进程桥接**：
  1. WSL：`host/opencode_source.py` 以文件 sink 运行（`oc_poll.py`）→ 追加事件行到 `D:\buddy_flash\oc_events.log`
  2. Windows：`file_to_serial.py` 尾随该文件 → 写 COM5
- 该桥接是**E2E 验证脚手架**，不是交付件；交付件是 `opencode_source.py`（host 单测 69/OK 覆盖）。

## 1. 实测（2026-10-17）

- 轮询器从真实库读出本会话为 **busy** → 写出 `task.start`（title=会话标题、project=`home`）。
- 设备：**WORKING**（眯眼 + `>`）+ 任务卡（标题/项目/时长）。
- 本回合 assistant 消息完成 → 轮询器发出 `task.done` → 设备转 **DONE** 并**播报**（用户确认）。
- 用户确认："是"（WORKING + 卡片；转 DONE 并出声）。

## 2. 证据分级

| 结论 | 分级 |
| --- | --- |
| 真实 OpenCode 库 → busy/idle 判定 | **本次实测** |
| 事件线经桥接到达设备并驱动状态/卡 | **本次实测** |
| WORKING → DONE（边沿）+ 语音 | **本次实测**（人工） |
| `waiting`（需要确认） | **未实现**（DB 无可证形态，见 spike） |

## 3. 残余风险

| 项 | 说明 |
| --- | --- |
| 桥接两进程 | 生产化需把"WSL 轮询 + Windows 串口"收敛为一个部署单元（或让入口跑在 WSL 并用 TCP） |
| 会话选择 | 取 `time_updated` 最新的顶层 session；多项目时可能不是用户认为的"当前" |
| schema 内部 | `opencode.db` 属内部实现，升级需复验（spike §5） |
| `waiting` | 未做 → `INPUT_REQUIRED` 仍只由脚本事件验证（M4 传输） |
