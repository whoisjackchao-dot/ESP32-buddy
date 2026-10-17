# M4 · P1-2a：OpenCode 真源轮询器 · T 类 RED→GREEN 记录

> 日期：2026-10-17　类别：**T**（真源轮询器，测试先行）
> 结论范围：**host 单测**（temp SQLite 注入，**不开真实 DB、不开真实串口**）通过；**不构成设备实机验收**（`E2E-01` 真机段未做）。
> 契约：`contracts/M4-事件入口与OpenCode接入契约清单.md` §9.1（线协议）/§9.3（主机侧传输复用，2026-10-17 冻结）；真源事实与方案依据 `evidence/M4/2026-10-17-OpenCode真源-spike.md`（§2 事实 / §4 推荐方案 P1-2a）

## 证据字段

| 字段 | 内容 |
| --- | --- |
| 需求 ID | R11（OpenCode 真源驱动设备状态） |
| 契约 ID | M4 §9.1/§9.3（线格式与传输复用）＋ spike 报告 §4 P1-2a（只读 SQLite 轮询 → busy/idle/done/error） |
| 用例 ID | `E2E-01`（host 侧派生逻辑；真机段未做） |
| 待测组件 | `host/opencode_source.py`（`connect_ro`/`latest_session`/`latest_assistant`/`derive_state`/`OpenCodeSource`/`heartbeat_due`/`main` CLI）＋ `host/tests/test_opencode_source.py` |
| 代码版本 | `host/opencode_source.py` sha256 `87ad08ef6525f481bcd2e02e9c9f513e9bf0add166a4d3e3c541a6d514fdbc6b`；`host/tests/test_opencode_source.py` sha256 `92b4c27c001f2878f57c0f30a99758df01514b4da635667a543bee539943397f` |
| 环境 | Linux x86_64；Python 3.14.4，**仅 stdlib**（`sqlite3`/`json`/`argparse`/`unittest`；pyserial 仅真实串口路径经 `pet_transport.SerialSink` 惰性 import，测试不依赖） |
| 人工/实机证据 | **无**（测试全部 temp DB ＋注入 sink；真实 OpenCode DB 未打开，另以一次性脚本对 temp 库验证 `mode=ro` 写入被拒 `attempt to write a readonly database`） |

## 1. RED

命令 `./host/run-tests.sh`（`host/opencode_source.py` 为**空壳 stub**：函数/类/常量在，实现 `raise NotImplementedError`；既有 55 条不受影响）→

```
Ran 69 tests in 0.546s

FAILED (errors=14)
```

退出码 **1**：既有 55 条全绿，新增 14 条全部 `NotImplementedError`（状态未派生、会话未选、轮询未产出事件、CLI 未实现）。

## 2. GREEN

实现后 `./host/run-tests.sh` →

```
Ran 69 tests in 0.533s

OK
```

退出码 **0**，输出无 warning/error（含既有 55 条，未放宽任何断言）。

| 用例 | 断言要点 |
| --- | --- |
| `E2E-01` | `derive_state`：无消息 → `idle`；`time` 缺 `completed` → `busy`；`completed` 且无 `error` → `idle`；`completed` 且 `error` 非空 → `error` |
| `E2E-01` | `latest_session`：取 `time_updated` 最新**顶层** session（`parent_id` 非 null 的子 session 更新也不入选）；无 session → `None` |
| `E2E-01` | `latest_assistant`：按 `time_updated` 降序跳过 **malformed JSON 行与 user 行**，返回首条合法 assistant；无 → `None` |
| `E2E-01` | `OpenCodeSource.poll` 边沿序列：busy → `task.start`（`project` 取 directory 去尾 `/` 后 basename）；仍 busy → `None`；完成 → `task.done`；仍 idle → `None`；出错 → `task.error`；再完成（error→idle 边沿）→ `agent.idle`（**不带** title/project）；无 session → `None` |
| `E2E-01` | `heartbeat_due`：从未发送（`None`）→ `True`；距上次 < interval → `False`；≥ interval → `True` |
| `E2E-01` | `main(["--once", ...], source_factory=…, sink_factory=…)`：返回 **0**；工厂收到 `db=path`/`port`；sink 恰收到一行 `task.start`（dict，交 `ensure_line` 帧化） |

## 3. 参数（提案值，**待批准**）

| 符号 | 值 | 依据 |
| --- | --- | --- |
| `DEFAULT_DB` | `~/.local/share/opencode/opencode.db`（expanduser） | spike §2 实测路径 |
| 只读打开 | `sqlite3.connect("file:<path>?mode=ro", uri=True)` | spike §5（WAL 并发只读；temp 库实测写入被拒） |
| 轮询间隔 `--interval` | `1.0` s | spike §4 P1-2a（~1 s 轮询） |
| 心跳间隔 `--heartbeat` | `5.0` s | 本项目提案（纯函数 `heartbeat_due`，`>=` 判到期） |
| `--port` 默认 | `COM5` | §9.3 与 `pet_sender` 一致（USB-Serial-JTAG CDC） |
| 当前会话判定 | 顶层 session 按 `time_updated` 最新 | spike §4（多 session 取最新）；spike §5 已登记"可能不符直觉"风险 |
| 注入点签名 | `source_factory(db_path)`；`sink_factory(port, baud)`（与 `pet_sender.open_sink` 同形） | §9.3 契约留白（工厂签名未规定），选与既有发送端一致的读法 |
| 心跳计时 | 仅在**发送心跳后**重置（发事件不重置）；`--once` 仍走一次完整迭代（无事件则可能发一条 heartbeat 后退出） | 循环语义按任务规定逐字实现（`if ev … elif heartbeat_due …`） |

## 4. 残余风险

| 风险 | 说明 |
| --- | --- |
| **内部 schema／版本漂移** | `opencode.db` 是 OpenCode **内部实现**（spike §5），本实现对 1.18.34 的表/列（`session.parent_id`、`message.data`）有效；OpenCode 升级需复验 |
| **waiting 未实现** | `INPUT_REQUIRED` 无持久化证据（spike §3 未验证），本增量只派生 `busy/idle/done/error` 四态；`agent.input_required` 待 P1-2b 一次真实权限提示后接 |
| **真机联调未做** | 全部单测用 temp SQLite ＋注入 sink，**不开真实 DB、不开真实串口**；`E2E-01` 真机段（真实任务 → WORKING / DONE+语音 / FAILED）待实机验证 |
| 会话选择 | 多 session/多项目并行时"当前会话=最新 `time_updated`"可能与用户直觉不符（spike §5 已登记） |
| 常量待批准 | 见 §3 表（`--interval`/`--heartbeat` 等均为提案值） |
