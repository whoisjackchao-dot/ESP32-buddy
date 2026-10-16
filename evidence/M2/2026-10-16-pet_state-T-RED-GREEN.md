# M2 · pet_state 状态机 · T 类 RED→GREEN 记录

> 日期：2026-10-16　类别：**T**（业务逻辑，测试先行）
> 结论范围：**host 单测**通过；**不构成设备实机验收**。
> 契约：`contracts/M2-状态机与动画契约清单.md` §7（2026-10-16 冻结）

## 证据字段

| 字段 | 内容 |
| --- | --- |
| 需求 ID | R10（6 态）、R11（事件模型）、R23（失联灰化） |
| 契约 ID | M2 契约 §7（状态转移 / done 边沿 / 失联） |
| 用例 ID | `ST-01`…`ST-07`（状态）、`LINK-01`（失联） |
| 待测组件 | `firmware/components/pet/pet_state.c` |
| 输入 fixture | 无（纯逻辑）；常量经 `-D` 注入 |
| 代码版本 | `pet_state.c` sha256 `327201a7…2176a`；`tests/test_pet_state.c` sha256 `729aaa4c…e4f0c` |
| 环境 | WSL2 Ubuntu 26.04；`cc` = gcc 15.2.0 |
| 人工/实机证据 | **无** |

## 1. RED（因缺少行为而失败）

命令：`./tests/run.sh`（`pet_state.c` 为最小空壳）

- 结果：`test_pet_state` **39 checks, 26 failed**，退出码 1
- 失败断言示例（均为业务断言，非环境错误）：
  - `view_state() == PET_STATE_WORKING`（TASK_START 未生效）
  - `pet_state_take_sound(s) == PET_SOUND_INPUT`
  - `view_state() == PET_STATE_DONE`（done 边沿未合成）
  - `view_state() == PET_STATE_OFFLINE`（失联未判定）

## 2. GREEN

`./tests/run.sh` → `test_pet_render` 13/0、`test_pet_state` **39 checks, 0 failed**，退出码 **0**；
**重复运行第二次同样 PASS**（确定性）。

覆盖的行为：

| 用例 | 断言要点 |
| --- | --- |
| `ST-01` | 新建 → IDLE / NORMAL / 非灰 / 无待播 |
| `ST-02` | `TASK_START` → WORKING，**静默** |
| `ST-03` | `AGENT_INPUT_REQUIRED` → INPUT_REQUIRED，播报一次（取走即清） |
| `ST-04` | `TASK_DONE` → DONE + HAPPY + 播报一次 |
| `ST-05` | `TASK_ERROR` / `AGENT_BLOCKED` → FAILED + SAD + 播报 |
| `ST-06` | **done 边沿**：WORKING 收 IDLE → 去抖窗口内仍是 WORKING；到点转 DONE 并播报 |
| `ST-07` | **抖动取消**：去抖窗口内再来 `TASK_START` → 保持 WORKING，**不播报** |
| `ST-08` | IDLE 收 IDLE → 保持 IDLE，**不播报** |
| `ST-09` | DONE 驻留 5000ms 后回 IDLE |
| `LINK-01` | 无事件超 15s → OFFLINE，`gray=true`，闭眼帧 |
| `LINK-02` | `LINK_UP` 恢复进 OFFLINE 前的状态 |
| `LINK-03` | 新事件刷新失联计时，不误判 |

## 3. 残余风险

| 风险 | 说明 |
| --- | --- |
| 常量仍为**提案值** | `DONE_DWELL=5000` / `DEBOUNCE=1500` / `LINK=15000`；`DEBOUNCE` **无上游证据**，本项目自定 |
| `done` 误报/漏报 | 契约明确**不承诺**零误报/零漏报；真实 Agent 抖动未在真实数据上验证 |
| 播报为**单槽** | `apply` 会覆盖未取走的待播；M5 实现语音时需复核是否要排队 |
| 渲染层未跟上 | `gray` / INPUT 标记 / 动画均未实现 → **6 态尚不可"目视可辨"**（见下） |
| 无实机证据 | 定时/漂移在真机 `esp_timer` 上未验证 |

## 4. 与模拟器的关系

主机模拟器（`tools/sim.sh`）用**同一份** `pet_state.c`/`pet_render.c` 渲染 6 态位图，
供目视。当前图中 **idle/working/input 三态相同**、**offline 未变灰**——这正是上表
"渲染层未跟上"的可见后果，**不是模拟器缺陷**。
