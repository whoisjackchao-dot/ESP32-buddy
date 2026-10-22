# ESP32-buddy

> 一个给 AI 编程 Agent 配的桌面宠物：盯着 OpenCode（先支持它），用动画、任务卡和一句短语音告诉你它什么时候需要你、干完了、还是失败了。
>
> **本仓库是净室重实现（clean-room reimplementation）**：借鉴 [second-state/vibebuddy](https://github.com/second-state/vibebuddy) 的**设计与事件模型**，不搬其 Rust 代码。固件为 **C / ESP-IDF**。

**当前状态：以 [文档索引](./ESP32-buddy_文档索引.md) 为准。**
阶段 3（开发）接近收尾：**M1–M8 已实现**，其中 M1/M2/M3/M4/M5/M6/M7/M8 的多数项已通过**真机（H）**验证；
**M9（验收与交付）正在准备**：`acceptance/` 下有 G1–G7 执行清单与可校验 manifest，门槛本身**待设备执行**。

## 1. 目标与范围

- **先攻硬件**：M5Stack StickS3（K150，ESP32-S3，135×240 彩屏，ES8311）。
- **三个 feature**：宠物动画、番茄钟、任务卡（≤3 张）。
- **不追求**通用框架：只要求"设计上分层，便于将来移植"。

## 2. 仓库结构

```
.
├── ESP32-buddy_文档索引.md          # ★ 单一入口：任何会话先读它
├── ESP32-buddy_需求与决策基线.md     # R / G / Q / B（需求、门槛、决策、缺口）
├── ESP32-buddy_M0-M9开发计划.md      # 里程碑、DoR、冻结时间表、停止条件
├── ESP32-buddy_测试用例与追踪矩阵.md  # R → 用例 → M → 证据
├── contracts/                       # 契约冻结、ADR、参数冻结表
├── evidence/                        # 证据日志（按里程碑分目录，T/C/H/D 分级）
├── acceptance/                      # M9：G1–G7 执行清单 + manifest + 许可登记
├── firmware/                        # ESP-IDF 工程（components/pet 为核心）
├── host/                            # 主机侧：HTTP 入口、单进程桥、串口发送、OpenCode 真源、回窗口
├── tests/                           # host 单测（C，589 checks）
├── tools/                           # 构建/语音/立绘/打包/manifest/门槛预检
├── flash/                           # 可直接带走的烧录包
├── sim/                             # 主机端预览图（BMP）
├── NOTICE.md                        # 第三方素材、许可、商标边界
└── LICENSE                          # 代码 MIT
```

## 3. 与上游的关系（必读）

| 项 | 来源 | 许可 | 说明 |
|---|---|---|---|
| 设计 / 事件模型 / 状态机 | vibebuddy（GPL-3.0-or-later） | 只借鉴**设计**，不复制代码 | 净室重实现，本项目代码保持 MIT |
| 立绘（48×64 4bpp，12 帧） | **本项目原创**（`tools/make-look.py` 程序化生成） | **MIT** | 上游"机器人"实为代码绘制、仓库无成品资产；故自绘（`Q18`） |
| 名称与角色 | vibebuddy | 保留商标权 | **本项目不得使用 "VibeBuddy" 名称或角色商标** |

详见 [NOTICE.md](./NOTICE.md)。

## 4. 开发环境

| 项 | 工具 | 状态 |
| --- | --- | --- |
| 固件 | ESP-IDF **v5.5.4**（release/v5.5），目标 esp32s3 | ✅ 可用（`tools/idf-env.sh` 包装 venv 与代理） |
| 固件 | xtensa-esp-elf 14.2.0 | ✅ |
| 构建 | 构建目录必须放 **WSL 原生盘**（`-B ~/esp-build/...`），放 `/mnt/c` 慢约 30 倍 | ⚠️ 已知 |
| 通用 | 系统 python3 为 3.14，ESP-IDF 需 ≤3.13 → 用 `tools/idf-env.sh` | ✅ |
| host 单测 | `./tests/run.sh`（C，**589 checks**）+ `./host/run-tests.sh`（Python，**77 tests**） | ✅ |
| 编译期差异 | gcc 15.2 会因 `-Wformat-truncation` 报错、且需显式 `-lm`；`zig cc` 不会 | ✅ 已修（`96e1bce`） |

> 新机器无系统 `cc` 时：`CC=tools/host-cc.sh bash tests/run.sh`（自动选 `cc` 或用户态 `zig cc`）。

### 常用命令

```bash
# host 单测
./tests/run.sh && ./host/run-tests.sh

# 构建固件
cd firmware && source ../tools/idf-env.sh && idf.py -B ~/esp-build/buddy build

# 打包烧录件
./tools/package-flash.sh --build

# M9 门槛预检（无需设备）
python3 tools/gate-host-smoke.py

# 起单进程桥（HTTP 入口 → 串口）
python3 host/pet_bridge.py --serial COM5 --port 8765

# 交付 manifest（含构建产物哈希）
python3 tools/make-manifest.py --build-dir ~/esp-build/buddy
python3 tools/make-manifest.py --check --build-dir ~/esp-build/buddy

# 主机端预览（6 态 / 场景 / 事件线）
./tools/sim.sh
```

## 5. 许可

- **代码**：MIT（见 `LICENSE`）。
- **立绘与派生美术**：本项目**原创**，随代码 MIT（未使用上游美术，不涉及 CC BY-SA）。
- **内置方法论**：`.opencode/skills/dayu/` 为 MIT，见其 `LICENSE`。

## 6. 方法论（dayu）

本项目按 [dayu](.opencode/skills/dayu/SKILL.md) 规范推进，**该技能已随仓库内置**——
在新机器上 clone 本仓库后，即可按同一规范继续开发与测试：

- 单一入口：[文档索引](./ESP32-buddy_文档索引.md)（任何会话先读它）
- 需求/决策：[需求与决策基线](./ESP32-buddy_需求与决策基线.md)
- 规划：[M0–M9 开发计划](./ESP32-buddy_M0-M9开发计划.md)
- 契约：[contracts/](./contracts/README.md)
- 追踪：[测试用例与追踪矩阵](./ESP32-buddy_测试用例与追踪矩阵.md)
- 证据：[evidence/](./evidence/)
- 验收：[acceptance/](./acceptance/README.md)
- 模板：`.opencode/skills/dayu/references/templates/`

> 三条最贵的铁律（dayu）：**未批准不当作事实**；**文档不等于执行**（只有 RED→GREEN 证据能证明行为）；**两种"完成"分开**（文档交付 ≠ 产品交付）。
