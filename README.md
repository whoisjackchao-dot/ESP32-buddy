# ESP32-buddy

> 一个给 AI 编程 Agent 配的桌面宠物：盯着 Codex / Claude Code / OpenCode，用动画、任务卡和一句短语音告诉你它什么时候需要你、干完了、还是失败了。
>
> **本仓库是净室重实现（clean-room reimplementation）**：借鉴 [second-state/vibebuddy](https://github.com/second-state/vibebuddy) 的**设计与事件模型**，不搬其 Rust 代码。固件为 **C / ESP-IDF**。

**当前状态：以 [文档索引](./ESP32-buddy_文档索引.md) 为准。** 当前处于 **阶段 3（开发）**：M1–M7 与 M8 增量 1/1b/2 已实现并有证据；**M8 多卡紧凑版式的真机 H 待做，M9 验收/交付未开工**。host 侧当前 **C 599/0**、**Python 98/98**，CI 覆盖 gcc/clang、Python、IDF 5.5.4 构建与模拟器；H 类仍以真机记录为准，模拟器不替代 H。

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
├── evidence/                        # 证据日志，按里程碑分目录
├── firmware/                        # ESP-IDF 固件（C）
├── host/                            # 主机侧：入口/传输/OpenCode/统一守护
├── tests/                           # host C 单测（T 类）
├── tools/                           # 构建、模拟器、验收、发布脚本
├── docs/                            # 可复现构建、Q10 决策包、计划
├── .github/workflows/ci.yml         # gcc/clang + Python + IDF 5.5.4 + sim
├── NOTICE.md                        # 第三方许可登记与商标边界
└── LICENSE                          # 代码 MIT；立绘为本项目自绘
```

## 3. 与上游的关系（必读）

| 项 | 来源 | 许可 | 说明 |
|---|---|---|---|
| 设计 / 事件模型 / 状态机 | vibebuddy（GPL-3.0-or-later） | 只借鉴**设计**，不复制代码 | 净室重实现，本项目代码可保持 MIT |
| 立绘（机器人 48×64 4bpp，12 帧） | **本项目自绘**（`tools/make-look.py`） | 随项目发布 | 未复制上游美术；设计灵感来自 vibebuddy 的桌面宠物概念 |
| 名称与角色 | vibebuddy | **保留商标权** | **本项目不得使用 "VibeBuddy" 名称或角色商标**，故取名 ESP32-buddy |

详见 [NOTICE.md](./NOTICE.md)。

## 4. 开发环境

沿用既有工程环境（与 StickS3 × OpenCode 项目一致）：

| 项 | 工具 | 状态 |
| --- | --- | --- |
| 固件 | ESP-IDF **v5.5.4**，目标 esp32s3 | 🟡 CI 用 `espressif/idf:v5.5.4`；本机 `~/esp/esp-idf` 是 5.5.5、`esp-idf-v6` 是 6.0.1 → `tools/build-firmware.sh --check` 会拒绝 |
| 固件 | xtensa-esp-elf 14.2.0 | ✅ 原开发机 / CI 容器 |
| 构建 | 构建目录放 **WSL/Linux 原生盘**（`-B ~/esp-build/...`），放 `/mnt/c` 慢约 30 倍 | ⚠️ 已知 |
| 通用 | 系统 python3 3.14；ESP-IDF 用自带 venv（`tools/idf-env.sh`） | ✅ 已建立 |
| 通用 | host C 单测 `bash tests/run.sh`（T 类），需要 `cc` + `-lm` | ✅ gcc 15 / clang / CI 均可复现 **599/0** |
| 通用 | host Python `./host/run-tests.sh`（stdlib） | ✅ **98/98** |
| 离线视觉 | `bash tools/sim.sh` → `sim/*.bmp` + `sim/MANIFEST.txt` | ✅ 多卡/菜单/番茄钟/小剧场景 |
| 主机产品化 | `python3 host/pet_service.py --port COM5` | ✅ 单进程 HTTP + OpenCode + 串口；可选 `PET_EVENT_TOKEN` |

> 换机无系统 `cc` 时，仍可用 `CC=tools/host-cc.sh bash tests/run.sh`。
> 固件构建/发布清单见 [`docs/reproducible-build.md`](./docs/reproducible-build.md)；CI 见 `.github/workflows/ci.yml`。
> 真机验收按 [`tools/acceptance/README.md`](./tools/acceptance/README.md) 执行；**模拟器/BMP 不是 H 证据**。

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
