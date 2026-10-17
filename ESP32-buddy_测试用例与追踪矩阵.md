# ESP32-buddy 测试用例与追踪矩阵

> 依据：dayu `references/ids.md`（追踪链路 `R → C/Q → 用例 → M → 证据`）与 `04-testing.md`。
> 状态枚举（只用这五个，见 dayu `evidence.md` §10）：
> `NOT_RUN` / `BLOCKED` / `FAIL` / `PASS` / `NOT_APPLICABLE_APPROVED`
>
> **执行纪律**：T 类必须 host 单测 PASS 才算；H 类必须有真机观察记录。**skip 不算 PASS。**

## 1. 用例 ID 规则

| 前缀 | 域 |
| --- | --- |
| `LCD-` | 显示/绘制（立绘、surface、文本） |
| `ST-` | 状态机 |
| `LINK-` | 链路与失联 |
| `UI-` | 版式与界面 |
| `CARD-` | 任务卡 |
| `AUD-` | 语音 |
| `BTN-` | 按键 |
| `POMO-` | 番茄钟 |
| `LEI-` | 休闲 |
| `E2E-` | 端到端 |
| `SEC-` | 安全/鉴权 |

## 2. 追踪表（需求 → 用例 → 节点 → 状态）

| 需求 | 用例 | 节点 | 层级 | 状态 | 证据 |
| --- | --- | --- | --- | --- | --- |
| R01 做一只宠物 | `LCD-01` `UI-01` | M1 | T | `PASS`（host） | `evidence/M1/2026-10-16-pet_render-T-RED-GREEN.md` |
| R02 先做 M5StickS3 | `UI-01`（版式按 135×240） | M1/M3 | T | `PASS`（host） | 同上 + `evidence/M2/…渲染半段-T.md` |
| R03 三 feature：动画/番茄钟/任务卡 | `UI-02`（动画/状态）、`POMO-*`、`CARD-*` | M2/M7/M3 | T | `UI-02` `PASS`；`POMO-*` `PASS`（逻辑，M7 增量1）；`CARD-*` 见下 | `evidence/M2/2026-10-16-pet_render-渲染半段-T.md`、`evidence/M7/2026-10-16-番茄钟逻辑-T.md` |
| R05 采用 vibebuddy 事件模型 | `ST-01`…`ST-09` | M2 | T | `PASS` | `evidence/M2/2026-10-16-pet_state-T-RED-GREEN.md` |
| R06 净室重实现 | —（过程约束，非行为） | 全程 | D | `PASS`（审查） | `NOTICE.md` §4 |
| R07 立绘 CC BY-SA 可用 | —（许可，非行为） | M1 | D | `NOT_APPLICABLE_APPROVED`（改用**自绘**立绘，未用上游素材） | `NOTICE.md` §1、`evidence/M1/…T-RED-GREEN.md`（资产修订） |
| R08 装进新仓库 | —（组织约束） | 全程 | D | `PASS` | 本仓库 |
| R09 开源且不用 "VibeBuddy" 商标 | —（许可，非行为） | 全程 | D | `PASS` | `NOTICE.md` §2、已发布 GitHub |
| R10 顶层产品目标（6 态/卡/语音/战绩/三模式） | `ST-*` `UI-02` `CARD-*` `AUD-*` `POMO-*` `LEI-*` | M2…M8 | T/H | 部分 `PASS`，其余见各节点 | 见下 |
| R11 OpenCode 首选接入 | `E2E-01` | M4 | C+H | ✅ **`PASS`**（**真实 OpenCode 真源**：库轮询 busy/idle → 设备 WORKING→DONE+语音，2026-10-17 真机）；`waiting` 未做；桥接为验证脚手架 | `evidence/M4/2026-10-17-OpenCode真源-spike.md`、`evidence/M4/2026-10-17-OpenCode轮询器-T.md`、`evidence/M4/2026-10-17-OpenCode端到端-H.md` |
| R12 协议不绑定客户端 | `E2E-02`（任意脚本发事件） | M4 | C+H | `PASS`——**端到端**：脚本 → 主机（入口核心 §8 + 传输 §9）→ 设备 → 状态/卡/语音均生效（2026-10-17 真机） | `evidence/M4/2026-10-16-主机入口核心-T.md`、`evidence/M4/2026-10-17-传输端到端-H.md` |
| R13 任务卡最多 3 张（≤3、新在上） | `CARD-03` | M8 | T | `PASS`（host 589/0；设备版式 `NOT_RUN`） | — |
| R14 三模式（值班/番茄钟/休闲） | `POMO-*` `LEI-*` | M7/M8 | T | 番茄钟逻辑 `PASS`（M7 增量1）；**模式切换（KEY2）属 M6/H**；休闲 `NOT_RUN`（M8） | `evidence/M7/2026-10-16-番茄钟逻辑-T.md` |
| R15 语音：需确认/完成/失败各一次 | `AUD-01`…`AUD-06` | M5 | T+H | `AUD-01..04` `PASS`（逻辑/资产）；**`AUD-05` `PASS`**（2026-10-17 真机听到三句）；`AUD-06`（不阻塞主循环）仍 `NOT_RUN`（未测帧率） | `evidence/M5/2026-10-16-语音逻辑-T.md`、`evidence/M4/2026-10-17-传输端到端-H.md` |
| R16 当日战绩轮播、按日清零、重启不丢 | `POMO-06` `UI-07` | M7 | T | `POMO-06`（按日清零逻辑）`PASS`；轮播 `UI-07`、**重启不丢（持久化）** `NOT_RUN`（M7 增量 2） | `evidence/M7/2026-10-16-番茄钟逻辑-T.md` |
| R17 休闲（5/30/90 分钟、关背光） | `LEI-01`…`LEI-05` | M8 | T | **`LEI-01..05` `PASS`**（状态机逻辑，M8 增量1）；**小剧美术 / 背光 / NTP 未做** | `evidence/M8/2026-10-17-休闲状态机-T.md` |
| R18 按键操作 | `BTN-01`…`BTN-06` | M6 | T+H | `BTN-01..04` **`PASS`**（逻辑 + **真机**：切模式 / 番茄钟控制，无误触发）；**`BTN-05`（回窗口）`PASS`**（真机端到端）；`BTN-06`（静音）接线、**听感未验**；菜单未做 | `evidence/M6/2026-10-17-按键逻辑-T.md`、`evidence/M6/2026-10-17-按键-H.md`、`evidence/M6/2026-10-17-回窗口-H.md` |
| R20 分层（state/render/hal） | —（结构约束） | M1 | D | `PASS`（审查：仅 `pet_hal_sticks3.c` 碰硬件） | `evidence/M1/2026-10-16-固件构建-D.md` §4 |
| R21 逻辑可 host 测 | 全部 host 单测 | M1/M2/M3 | T | `PASS` | `tests/run.sh` |
| R22 复用既有链路 + 事件转换 | `E2E-01` `E2E-03` | M4 | C | `E2E-03`（非法输入：不崩/回错/不广播）`PASS`（M4 增量2a）；`E2E-01` 全链路 `NOT_RUN`（传输未接） | `evidence/M4/2026-10-16-主机入口核心-T.md` |
| R23 失联灰化 | `LINK-01` `LINK-02` `UI-05` | M2 | T | `PASS` | `evidence/M2/…pet_state-T…`、`…渲染半段-T…` |
| R24 番茄钟在设备上跑 | `POMO-01`…`POMO-12` | M7 | T | `PASS`（host 逻辑，M7 增量1）；**真机 H** `NOT_RUN` | `evidence/M7/2026-10-16-番茄钟逻辑-T.md` |

## 3. 用例明细（已执行者）

| 用例 | 断言要点 | 层级 | 状态 | 所在测试 |
| --- | --- | --- | --- | --- |
| `LCD-01` | 非法 magic / 长度不符被拒；合法包可解析 | T | `PASS` | `tests/test_pet_render.c` |
| `LCD-02` | 不透明像素按 **2×** 绘制且颜色=调色板值；透明像素不覆盖；右/下/负原点越界裁剪（guard 不越写） | T | `PASS` | 同上 |
| `LCD-03` | 8×8 文本：字形落在本字格内；`max_chars` 生效（第 3 格起空白） | T | `PASS` | `tests/test_pet_card.c` |
| `ST-01` | 新建态 = IDLE/NORMAL/非灰/无待播 | T | `PASS` | `tests/test_pet_state.c` |
| `ST-02` | `TASK_START` → WORKING 且**静默** | T | `PASS` | 同上 |
| `ST-03` | `INPUT_REQUIRED` → 该态 + 播报一次（取走即清） | T | `PASS` | 同上 |
| `ST-04` | `TASK_DONE` → DONE + HAPPY + 播报 | T | `PASS` | 同上 |
| `ST-05` | `TASK_ERROR`/`AGENT_BLOCKED` → FAILED + SAD + 播报 | T | `PASS` | 同上 |
| `ST-06` | **done 边沿**：空闲边沿经去抖窗后转 DONE 并播报 | T | `PASS` | 同上 |
| `ST-07` | **抖动取消**：去抖窗内再来 `TASK_START` → 不转 DONE、**不播报** | T | `PASS` | 同上 |
| `ST-08` | 已在 IDLE 收 IDLE → 保持、不播报 | T | `PASS` | 同上 |
| `ST-09` | DONE 驻留 `PET_DONE_DWELL_MS` 后回 IDLE | T | `PASS` | 同上 |
| `LINK-01` | 无事件超 `PET_LINK_TIMEOUT_MS` → OFFLINE + `gray` + 闭眼帧 | T | `PASS` | 同上 |
| `LINK-02` | `LINK_UP` 恢复进 OFFLINE 前的状态 | T | `PASS` | 同上 |
| `LINK-03` | 新事件刷新失联计时，不误判 | T | `PASS` | 同上 |
| `UI-02` | 6 态渲染结果**两两不同**；WORKING/INPUT 头顶有标记、IDLE 无 | T | `PASS` | `tests/test_pet_render.c` |
| `UI-05` | 灰化后三通道相等；非对称立绘翻转后位置正确 | T | `PASS` | 同上 |
| `UI-06` | idle：眨眼期闭眼、非眨眼期睁眼；发呆期首轮 Nap 闭眼；WORKING 不眨眼 | T | `PASS` | `tests/test_pet_state.c` |
| `UI-03` | 有卡时卡片区有内容；无卡时卡片区**只有场景底色**；宠物区始终有内容 | T | `PASS` | `tests/test_pet_card.c` |
| `CARD-01` | 超长标题**不越界**（guard 完整、卡片右侧仍是底色） | T | `PASS` | 同上 |
| `CARD-02` | 时长格式：`0S`/`45S`/`59S`/`1M`/`90S→1M`/`1H`/`2H` | T | `PASS` | 同上 |
| `CARD-03` | 多卡栈：≤3、新在上、同名更新置顶、第 4 张丢最旧；1 张→大卡、≥2 张→紧凑 3 行不越界 | T | `PASS` | `evidence/M8/2026-10-17-多卡栈-T.md`（host 589/0） |
| `LINK-04` | 事件线解析：合法（全字段/空格/未知键/换行/超长截断）与非法（非 JSON、缺括号、数组、空串、version 错、缺 event、未知事件、类型不符、超长行）各自的错误码 | T | `PASS` | `tests/test_pet_link.c` |
| `LINK-05` | 六个事件名 → `pet_event_kind_t`；`device.heartbeat` 只刷新链路不改状态 | T | `PASS` | 同上 |
| `CARD-04` | 带卡片字段时 `has_card` 且 `card.state` 随事件映射；无字段时不产生卡片 | T | `PASS` | 同上 |
| `AUD-01` | 播报→资产 id 一一对应；`NONE`/越界值 → `PET_VOICE_NONE`（不崩） | T | `PASS` | `tests/test_pet_voice.c` |
| `AUD-02` | `should_play`：`NONE`→false、静音→false、三态未静音→true | T | `PASS` | 同上 |
| `AUD-03` | 静音默认关、可开可关（不持久化） | T | `PASS` | 同上 |
| `AUD-04` | 三段语音 PCM：存在、16-bit 对齐、时长 0.10–3.0s、非静音 | T | `PASS` | 同上 |
| `AUD-05` | 真机播报（I2S/ES8311）听得到、不爆音、不拉伸 | **H** | ✅ **PASS**（2026-10-17 真机由事件驱动，用户听到三句：需要你确认下 / 做完啦 / 出错了） | `evidence/M4/2026-10-17-传输端到端-H.md` |
| `AUD-06` | 播报不阻塞主循环（动画不掉帧） | T+H | ✅ **PASS**（播放前后 `fps=7` 不变） | `evidence/2026-10-17-P0收尾-验证.md` |
| `POMO-01` | init → IDLE、remaining==total==FOCUS_MS、计数 0、无铃 | T | `PASS` | `tests/test_pet_pomo.c` |
| `POMO-02` | SHORT→FOCUS；tick 推进 remaining | T | `PASS` | 同上 |
| `POMO-03` | 暂停不推进；继续后从暂停点累计 | T | `PASS` | 同上 |
| `POMO-04` | 专注到点 → FOCUS_DONE + 铃 + done_count=1 + focus_seconds=1500 | T | `PASS` | 同上 |
| `POMO-05` | 边界：elapsed==total 即到点 | T | `PASS` | 同上 |
| `POMO-06` | DONE 不自动推进、不重复计数 | T | `PASS` | 同上 |
| `POMO-07` | FOCUS_DONE→BREAK→BREAK_DONE→IDLE（休息不计统计） | T | `PASS` | 同上 |
| `POMO-08` | 专注中途放弃 → IDLE，不计完成 | T | `PASS` | 同上 |
| `POMO-09` | 响铃取走即清 | T | `PASS` | 同上 |
| `POMO-10` | 换日统计清零；同日不动 | T | `PASS` | 同上 |
| `POMO-11` | IDLE 下长按无变化 | T | `PASS` | 同上 |
| `POMO-12` | 暂停中放弃不计；随后全新一段 | T | `PASS` | 同上 |
| `POMO-05` | 进度环：0‰ 全轨道、250‰ 含 3 点(90°)起点在 12 点、1000‰ 全环、环心（洞内）不动 | T+H | ✅ `PASS`（T）+ **真机目视**（demo 变体：橙环顺时针、倒计时、满环+响铃、绿色休息循环） | `tests/test_pet_render.c`、`evidence/M7/2026-10-17-番茄钟视图与持久化-D.md` |
| `POMO-13` | 统计编解码：16B LE + `"PMC1"`、往返还原、坏 magic/短长度 → `false` 且不改结构、`cap<16` → 0 | T | `PASS` | `tests/test_pet_pomo.c` |
| `BTN-01` | 映射表 8 项（2 键 × 2 模式 × 短/长）正确 | T | `PASS` | `tests/test_pet_keymap.c` |
| `BTN-02` | 20 ms 采样、首次翻转生效 | T+H | `PASS`（逻辑）+ 真机无误触发 | `tests/test_pet_button.c` |
| `BTN-03` | 长按阈值 600 ms：<600 短按、≥600 长按只触发一次 | T | `PASS` | 同上 |
| `BTN-04` | 短按在**松键**时判定 | T | `PASS` | 同上 |
| `BTN-05` | 回窗口（`Q15`/`B08`）：KEY1 短按 → 设备控制线 → 主机守护 → 激活终端窗口 | C+H | ✅ **PASS**（真机端到端；用户确认；失焦/最小化激活 **5/5**） | `evidence/M6/2026-10-17-回窗口-H.md` |
| `BTN-06` | `KEY1` 长按静音（不持久化） | T+H | ✅ **PASS**（`muted=1` 时无声、恢复有声；复位回 `muted=0`） | `evidence/2026-10-17-P0收尾-验证.md` |
| `BTN-07` | 菜单：`KEY2` 长按开/关；`KEY1` 短按移选中；`KEY2` 短按执行（静音切换 / 状态只读关闭） | T+H | ✅ **PASS**（真机；用户"都正常"） | `tests/test_pet_menu.c`、`evidence/M6/2026-10-17-菜单-H.md` |
| `LEI-01` | 空闲 5 min → BORED（4:59 仍 DUTY） | T | `PASS` | `tests/test_pet_leisure.c` |
| `LEI-02` | 累计 30 min → SLEEPY | T | `PASS` | 同上 |
| `LEI-03` | 90 min **且夜间**(23–07) → LIGHTS_OUT；白天不熄 | T | `PASS` | 同上 |
| `LEI-04` | 小剧：首段 3 s、BORED 20–40 s / SLEEPY 120–300 s、id 轮换、同 seed 确定 | T | `PASS` | 同上 |
| `LEI-05` | 唤醒：`agent_active`（含断链）/ 任意按键 → 立即 DUTY 并重置空闲计时 | T+H | `PASS`（逻辑 + 真机按键唤醒） | 同上 / `evidence/M8/2026-10-17-小剧与背光-H.md` |
| `LEI-06` | 小剧真机可见（7 种）+ LIGHTS_OUT 关背光 + 按键唤醒亮屏 | H | ✅ **PASS**（demo 阈值） | `evidence/M8/2026-10-17-小剧与背光-H.md` |
| `E2E-02` | 主机入口核心：简式/富式（`tasks[0]` 优先）→ 设备线；`stats` 忽略；title 截 63；heartbeat 无线外字段；**loopback 集成** POST→200 且 sink 收线；**真机端到端**：脚本发事件 → 设备状态/卡/语音生效 | C+H | `PASS`（核心 + 端到端） | `host/tests/test_pet_ingest.py`、`evidence/M4/2026-10-17-传输端到端-H.md` |
| `E2E-03` | 非法输入：方法/路径/content-type/超长/非 JSON/version/event/事件名/类型 → 对应码与 HTTP 状态；**错误不广播**；不崩 | C | `PASS` | 同上 |
| `SEC-01` | 仅 loopback 绑定（`server_address[0]=="127.0.0.1"`） | C | `PASS`（loopback）；token 待定 | 同上 |

## 4. 未覆盖提示（dayu `ids.md` §6：每条 R 至少一个用例）

- `R11`（OpenCode）仍 **`NOT_RUN`**（真源映射未做，旧项目不在本机 → 阻塞）。
- `R12`/`R22` 的 `E2E-02`/`E2E-03` **主机核心已 `PASS`**（M4 增量2a）；但**端到端（到设备）`NOT_RUN`**（传输未接）→ 节点未验收。
- R13、R15、R17、R18 的用例多数已登记但 **`NOT_RUN`**（对应节点未开工），**不算已验收**。
- `R24`（番茄钟逻辑）与 `R16` 的 `POMO-06` 已 **`PASS`**（host 逻辑，M7 增量1）；其**真机（H）与持久化**部分仍未验 → 节点未验收。
- **H 类：已做（真机）**：2026-10-17 —— ① 刷机运行（`DISPLAY READY M1`/`PIXEL CARD READY`/`AUDIO READY`+心跳）+ 目视 M1 一帧+失联灰化（`evidence/2026-10-17-实机刷机-H.md`）；② **USB-Serial-JTAG 传输**驱动下，**6 态依次可辨、任务卡正确、三句语音听到**（`evidence/M4/2026-10-17-传输端到端-H.md`）。
- **仍未验**：`R11` OpenCode 真源；多卡（M8）；刷新率/时序**数值**；`AUD-06` 不阻塞主循环。
- **无变异测试**：尚未验证"注入缺陷能否被测试抓住"（`parameters` 待补）。

## 5. 执行记录汇总（最近一次）

| 测试 | 用例数 | 结果 |
| --- | --- | --- |
| `tests/test_pet_render.c` | 52 checks | `PASS`（0 failed） |
| `tests/test_pet_state.c` | 55 checks | `PASS`（0 failed） |
| `tests/test_pet_card.c` | 21 checks | `PASS`（0 failed） |
| `tests/test_pet_link.c` | 47 checks | `PASS`（0 failed） |
| `tests/test_pet_voice.c` | 28 checks | `PASS`（0 failed） |
| `tests/test_pet_pomo.c` | 96 checks | `PASS`（0 failed） |
| `tests/test_pet_linebuf.c` | 25 checks | `PASS`（0 failed） |

命令：`tests/run.sh`（退出码 0）。**合计 324 checks。** 证据：`evidence/M1/`…`evidence/M5/`、`evidence/M7/`、`evidence/M4/`。
（host 运行经用户态 `CC=/tmp/opencode/zb/cc.sh`（zig cc）；见 `evidence/2026-10-16-新机器环境基线.md`。）

**主机侧 Python（M4 增量 2a+2b + M6 回窗口）**：`./host/run-tests.sh` → **55 tests, OK**（`unittest`，仅 stdlib；入口核心 + 传输发送端 + 设备行解析/守护；不开真串口）。证据：`evidence/M4/2026-10-16-主机入口核心-T.md`、`evidence/M4/2026-10-17-传输发送端-T.md`、`evidence/M6/2026-10-17-回窗口-T.md`。
