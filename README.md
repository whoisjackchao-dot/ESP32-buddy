# ESP32-buddy

> 一个给 AI 编程 Agent 配的桌面宠物：盯着 Codex / Claude Code / OpenCode，用动画、任务卡和一句短语音告诉你它什么时候需要你、干完了、还是失败了。
>
> **本仓库是净室重实现（clean-room reimplementation）**：借鉴 [second-state/vibebuddy](https://github.com/second-state/vibebuddy) 的**设计与事件模型**，不搬其 Rust 代码。固件为 **C / ESP-IDF**。

**当前状态：以 [文档索引](./ESP32-buddy_文档索引.md) 为准。** 当前处于 **阶段 3（开发）**：M1–M5 的 host 侧（T 类）已实现并单测通过，M6–M8 未开工；**全部 H 类（真机）证据待刷机**。

## 1. 目标与范围

- **先攻硬件**：M5Stack StickS3（K150，ESP32-S3，135×240 彩屏，ES8311）。
- **想要的三个 feature**：宠物动画、番茄钟、任务卡（先做 1 张卡）。
- **不追求**通用框架：本阶段不做可复用的组件/HAL 抽象库；只要求"设计上分层，便于将来移植"。

## 2. 仓库结构

```
.
├── README.md
├── ESP32-buddy_文档索引.md          # ★ 单一入口：任何会话先读它
├── ESP32-buddy_需求与决策基线.md     # 阶段 0–1：R / G / Q / B
├── ESP32-buddy_M0-M9开发计划.md      # 阶段 2：里程碑、DoR、冻结时间表、授权
├── contracts/                       # 契约冻结与 ADR
│   └── M1-渲染分层契约冻结.md
├── evidence/                        # 证据日志，按里程碑分目录
├── NOTICE.md                        # 第三方素材署名与商标边界
└── LICENSE                          # 代码 MIT（待确认）；立绘 CC BY-SA 4.0
```

## 3. 与上游的关系（必读）

| 项 | 来源 | 许可 | 说明 |
|---|---|---|---|
| 设计 / 事件模型 / 状态机 | vibebuddy（GPL-3.0-or-later） | 只借鉴**设计**，不复制代码 | 净室重实现，本项目代码可保持 MIT |
| 立绘（机器人 48×64 4bpp） | vibebuddy 仓库 | **CC BY-SA 4.0** | 需署名；**衍生艺术**须同协议开放 |
| 名称与角色 | vibebuddy | **保留商标权** | **本项目不得使用 "VibeBuddy" 名称或角色商标**，故取名 ESP32-buddy |

详见 [NOTICE.md](./NOTICE.md)。

## 4. 开发环境

沿用既有工程环境（与 StickS3 × OpenCode 项目一致）：

| 项 | 工具 | 状态 |
| --- | --- | --- |
| 固件 | ESP-IDF **v5.5.4**（release/v5.5），目标 esp32s3 | ✅ 已装（**原开发机**；新机器未装 → 缺口 `B15`） |
| 固件 | xtensa-esp-elf 14.2.0 | ✅ 同上 |
| 构建 | 构建目录必须放 **WSL 原生盘**（`-B ~/esp-build/...`），放 `/mnt/c` 慢约 30 倍 | ⚠️ 已知 |
| 通用 | 系统 python3 为 3.14，ESP-IDF 需 ≤3.13 → 用 `tools/idf-env.sh` 包装 | ✅ 已建立 |
| 通用 | host 单测 `tests/run.sh`（T 类），需要 `cc` | ⚠️ 新机器无系统编译器时，用用户态 `zig cc` 作 `cc`（见 `B15`） |

> `tools/idf-env.sh` 已建立（包装 ESP-IDF 的 venv 与代理）。
> **换机注意**：新机器无系统 `cc` 时，用 `CC=tools/host-cc.sh bash tests/run.sh`（自动选 `cc` 或用户态 `zig cc`，**370/0**）。
> 固件构建可用 Windows 侧 ESP-IDF v5.5.4；详见 `evidence/2026-10-16-新机器环境基线.md` 与缺口 `B15`。

## 5. 许可

- **代码**：MIT（见 `LICENSE`）。
- **立绘与派生美术**：本项目为**自绘**（未使用上游美术，故不涉及 CC BY-SA），见 `NOTICE.md`。
- **内置方法论**：`.opencode/skills/dayu/` 为 MIT（版权属作者本人），见其 `LICENSE`。

## 6. 方法论（dayu）

本项目按 [dayu](.opencode/skills/dayu/SKILL.md) 规范推进，**该技能已随仓库内置**——
在新机器上 clone 本仓库后，即可按同一规范继续开发与测试：

- 单一入口：[文档索引](./ESP32-buddy_文档索引.md)（任何会话先读它）
- 需求/决策：[需求与决策基线](./ESP32-buddy_需求与决策基线.md)
- 规划：[M0–M9 开发计划](./ESP32-buddy_M0-M9开发计划.md)
- 契约：[contracts/](./contracts/README.md)（含冻结时间表与参数冻结表）
- 追踪：[测试用例与追踪矩阵](./ESP32-buddy_测试用例与追踪矩阵.md)
- 证据：[evidence/](./evidence/)
- 模板：`.opencode/skills/dayu/references/templates/`

> 三条最贵的铁律（dayu）：**未批准不当作事实**；**文档不等于执行**（只有 RED→GREEN 证据能证明行为）；**两种"完成"分开**（文档交付 ≠ 产品交付）。
