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
| R03 三 feature：动画/番茄钟/任务卡 | `UI-02`（动画/状态）、`POMO-*`、`CARD-*` | M2/M7/M3 | T | `UI-02` `PASS`；`POMO-*`/`CARD-*` 见下 | `evidence/M2/2026-10-16-pet_render-渲染半段-T.md` |
| R05 采用 vibebuddy 事件模型 | `ST-01`…`ST-09` | M2 | T | `PASS` | `evidence/M2/2026-10-16-pet_state-T-RED-GREEN.md` |
| R06 净室重实现 | —（过程约束，非行为） | 全程 | D | `PASS`（审查） | `NOTICE.md` §4 |
| R07 立绘 CC BY-SA 可用 | —（许可，非行为） | M1 | D | `NOT_APPLICABLE_APPROVED`（改用**自绘**立绘，未用上游素材） | `NOTICE.md` §1、`evidence/M1/…T-RED-GREEN.md`（资产修订） |
| R08 装进新仓库 | —（组织约束） | 全程 | D | `PASS` | 本仓库 |
| R09 开源且不用 "VibeBuddy" 商标 | —（许可，非行为） | 全程 | D | `PASS` | `NOTICE.md` §2、已发布 GitHub |
| R10 顶层产品目标（6 态/卡/语音/战绩/三模式） | `ST-*` `UI-02` `CARD-*` `AUD-*` `POMO-*` `LEI-*` | M2…M8 | T/H | 部分 `PASS`，其余见各节点 | 见下 |
| R11 OpenCode 首选接入 | `E2E-01` | M4 | C+H | `NOT_RUN`（增量 1 的设备侧解析已完成，**主机入口/映射未做**） | `evidence/M4/2026-10-16-事件线解析-T.md` |
| R12 协议不绑定客户端 | `E2E-02`（任意脚本发事件） | M4 | C | `NOT_RUN`（同上） | 同上 |
| R13 任务卡最多 3 张 | `CARD-03` | M8 | T | `NOT_RUN`（M3 只 1 张） | — |
| R14 三模式（值班/番茄钟/休闲） | `POMO-*` `LEI-*` | M7/M8 | T | `NOT_RUN` | — |
| R15 语音：需确认/完成/失败各一次 | `AUD-01`…`AUD-04` | M5 | T | `PASS`（逻辑与资产）；`AUD-05/06` H 类 `NOT_RUN` | `evidence/M5/2026-10-16-语音逻辑-T.md` |
| R16 当日战绩轮播、按日清零、重启不丢 | `POMO-06` `UI-07` | M7 | T | `NOT_RUN` | — |
| R17 休闲（5/30/90 分钟、关背光） | `LEI-01`…`LEI-05` | M8 | T | `NOT_RUN` | — |
| R18 按键操作 | `BTN-01`…`BTN-06` | M6 | T+H | `NOT_RUN` | — |
| R20 分层（state/render/hal） | —（结构约束） | M1 | D | `PASS`（审查：仅 `pet_hal_sticks3.c` 碰硬件） | `evidence/M1/2026-10-16-固件构建-D.md` §4 |
| R21 逻辑可 host 测 | 全部 host 单测 | M1/M2/M3 | T | `PASS` | `tests/run.sh` |
| R22 复用既有链路 + 事件转换 | `E2E-01` `E2E-03` | M4 | C | `NOT_RUN` | — |
| R23 失联灰化 | `LINK-01` `LINK-02` `UI-05` | M2 | T | `PASS` | `evidence/M2/…pet_state-T…`、`…渲染半段-T…` |
| R24 番茄钟在设备上跑 | `POMO-01`…`POMO-07` | M7 | T | `NOT_RUN` | — |

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
| `LINK-04` | 事件线解析：合法（全字段/空格/未知键/换行/超长截断）与非法（非 JSON、缺括号、数组、空串、version 错、缺 event、未知事件、类型不符、超长行）各自的错误码 | T | `PASS` | `tests/test_pet_link.c` |
| `LINK-05` | 六个事件名 → `pet_event_kind_t`；`device.heartbeat` 只刷新链路不改状态 | T | `PASS` | 同上 |
| `CARD-04` | 带卡片字段时 `has_card` 且 `card.state` 随事件映射；无字段时不产生卡片 | T | `PASS` | 同上 |
| `AUD-01` | 播报→资产 id 一一对应；`NONE`/越界值 → `PET_VOICE_NONE`（不崩） | T | `PASS` | `tests/test_pet_voice.c` |
| `AUD-02` | `should_play`：`NONE`→false、静音→false、三态未静音→true | T | `PASS` | 同上 |
| `AUD-03` | 静音默认关、可开可关（不持久化） | T | `PASS` | 同上 |
| `AUD-04` | 三段语音 PCM：存在、16-bit 对齐、时长 0.10–3.0s、非静音 | T | `PASS` | 同上 |
| `AUD-05` | 真机播报（I2S/ES8311）听得到、不爆音、不拉伸 | **H** | `NOT_RUN`（**设备侧播放已实现**，待刷机验证） | `evidence/M5/2026-10-16-设备播放-D.md` |
| `AUD-06` | 播报不阻塞主循环（动画不掉帧） | T+H | `NOT_RUN`（**设计为异步队列 + 独立任务**；实机未验） | 同上 |

## 4. 未覆盖提示（dayu `ids.md` §6：每条 R 至少一个用例）

- R11–R18、R22、R24 的用例已登记但 **`NOT_RUN`**（对应节点未开工），**不算已验收**。
- **H 类全部未执行**：真机目视（M1 的"一帧"、M2 的"6 态可辨"）依赖设备，见 `需求与决策基线` `B14`。
- **无变异测试**：尚未验证"注入缺陷能否被测试抓住"（`parameters` 待补）。

## 5. 执行记录汇总（最近一次）

| 测试 | 用例数 | 结果 |
| --- | --- | --- |
| `tests/test_pet_render.c` | 37 checks | `PASS`（0 failed） |
| `tests/test_pet_state.c` | 55 checks | `PASS`（0 failed） |
| `tests/test_pet_card.c` | 21 checks | `PASS`（0 failed） |
| `tests/test_pet_link.c` | 47 checks | `PASS`（0 failed） |
| `tests/test_pet_voice.c` | 28 checks | `PASS`（0 failed） |

命令：`tests/run.sh`（退出码 0）。**合计 188 checks。** 证据：`evidence/M1/`…`evidence/M5/`。
