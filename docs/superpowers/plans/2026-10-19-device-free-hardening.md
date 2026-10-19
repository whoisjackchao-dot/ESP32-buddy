# Device-free Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在没有 M5StickS3 真机的情况下，恢复可信测试基线、建立离线视觉/鲁棒性验证、收敛主机产品化路径，并准备 M9 验收交付件。

**Architecture:** 先修测试运行器与编译兼容性；再用同一份固件渲染代码扩展主机模拟器并加布局断言；随后把 HTTP 入口、OpenCode 轮询、串口收发整合为单一守护进程；最后补验收脚本、可复现构建与文档/决策包。所有 H 类真机证据仍保持 `NOT_RUN`，不用模拟器冒充。

**Tech Stack:** C11 / ESP-IDF 5.5.4 / Python 3 stdlib / GitHub Actions / BMP 模拟器。

**Spec:** 用户批准的 A+B+C 工作清单（2026-10-19 会话），以及 `ESP32-buddy_文档索引.md`、`ESP32-buddy_M0-M9开发计划.md`。

## Global Constraints

- 不改写已批准的需求/Q 决策；Q10 仍标“待批准”。
- T 类必须 host 单测 PASS；H 类必须真机，当前不得标 PASS。
- 不引入第三方 Python 依赖；host 侧继续只用 stdlib。
- 固件目标 ESP-IDF v5.5.4；CI 固定该版本。
- 不推送 tag / Release。

## Review Focus

- 编译器差异（gcc/clang/zig cc）导致的警告与链接差异。
- 多卡紧凑版式与宠物底部的 2 px 交叠。
- 串口断连/重连时写线程与读线程互相拖死。
- OpenCode DB 被锁、schema 变化、空库时守护进程不退出。
- HTTP 入口在串口不可用时不丢事件、不崩、不广播错误。

---

### Task 1: 修复 C 测试运行器与编译兼容性

**Files:**
- Modify: `tests/run.sh`
- Modify: `tools/sim.sh`
- Modify: `firmware/components/pet/pet_render.c:286-302,475-502`
- Test: `bash tests/run.sh`

**Interfaces:**
- Produces: `tests/run.sh` 在 gcc/clang 下返回 0 且输出 589 checks；`tools/sim.sh` 可直接执行。

- [ ] 用 gcc 15 复现 RED：`bash tests/run.sh` 因 format-truncation 失败；绕过警告后因缺 `-lm` 链接失败。
- [ ] 修 `draw_card_row` 与 `pet_render_menu` 的缓冲区/限宽，保留 `-Werror`。
- [ ] 给所有 C 编译命令加 `-lm`；编译失败时记录 `status=1` 并继续跑其余套件。
- [ ] 给 `tests/run.sh`、`tools/sim.sh`、`tools/package-flash.sh`、`tools/idf-env.sh`、`tools/voice-tts.sh` 补可执行位。
- [ ] GREEN：`bash tests/run.sh` 589/0；`./tests/run.sh` 可执行；`./tools/sim.sh` 生成 BMP。
- [ ] Commit。

### Task 2: CI（C 测试 + Python + 固件构建 + 模拟器）

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: push/PR 自动跑 gcc、clang、Python、ESP-IDF 5.5.4 构建、sim 编译。

- [ ] 写 workflow：`host-c` matrix（gcc/clang）、`host-python`、`firmware`（espressif/idf:v5.5.4）、`sim`。
- [ ] 本地校验 YAML 可解析、命令路径正确。
- [ ] Commit。

### Task 3: 扩展模拟器 + 多卡布局断言

**Files:**
- Modify: `tools/sim.c`
- Create: `tests/test_pet_layout.c`
- Modify: `tests/run.sh`

**Interfaces:**
- Produces: `sim/` 下新增 3 卡、菜单、番茄钟、7 小剧、失联场景 BMP + `sim/MANIFEST.txt`；`test_pet_layout` 断言卡片区不越界/行高正确。

- [ ] 写失败测试：3 卡场景最后一行不越过 y=236、每行 28 px、卡区 x 范围正确。
- [ ] 扩展 sim 场景与 FNV-1a 哈希输出。
- [ ] GREEN：`bash tests/run.sh`、`bash tools/sim.sh`。
- [ ] 检查多卡与宠物底部交叠；若需调整，记录到契约并同步测试。
- [ ] Commit。

### Task 4: 鲁棒性/故障注入测试

**Files:**
- Create: `host/tests/test_pet_robustness.py`
- Modify: `host/tests/test_pet_ingest.py`（如需要）

**Interfaces:**
- Produces: 非法 UTF-8、超长行、坏 tasks、DB 缺失/锁/坏 schema、串口异常的可重复测试。

- [ ] 写 RED 测试覆盖上述输入。
- [ ] 修必要的最小实现（错误码/异常边界）。
- [ ] GREEN：`./host/run-tests.sh` 全绿。
- [ ] Commit。

### Task 5: 统一主机守护进程 + 可选 token

**Files:**
- Create: `host/pet_service.py`
- Modify: `host/pet_daemon.py`（加 stop_event）
- Modify: `host/pet_entry_server.py`（可选 token）
- Create: `host/tests/test_pet_service.py`
- Modify: `host/tests/test_pet_ingest.py`

**Interfaces:**
- Produces: `PetService(config)` 单进程启动 HTTP + OpenCode 轮询 + 串口读/写；`python3 host/pet_service.py --port COM5 --db ...` 单命令运行。

- [ ] 写 RED 测试：HTTP 事件→fake serial；fake source→sink；设备控制线→focus；stop 后线程退出；token 缺失/错误 401。
- [ ] 实现 `PetService` 与 CLI；串口错误重连/退避；保持默认 loopback。
- [ ] GREEN：`./host/run-tests.sh` 全绿。
- [ ] Commit。

### Task 6: M9 验收脚本/runbook

**Files:**
- Create: `tools/acceptance/run_host_checks.sh`
- Create: `tools/acceptance/README.md`
- Create: `evidence/M9/TEMPLATE.md`

**Interfaces:**
- Produces: `bash tools/acceptance/run_host_checks.sh` 跑全部 T 类并生成 host 验收摘要；G1–G7 的 H 步骤/记录模板。

- [ ] 写脚本与 runbook，明确 H 未跑时不得写 PASS。
- [ ] 本地运行脚本。
- [ ] Commit。

### Task 7: 可复现构建/交付件

**Files:**
- Create: `tools/build-firmware.sh`
- Create: `tools/release-manifest.sh`
- Create: `docs/reproducible-build.md`
- Modify: `NOTICE.md`

**Interfaces:**
- Produces: IDF 5.5.4 检查、构建目录固定、manifest（commit/IDF/hash）、第三方许可登记。

- [ ] 写脚本与文档；对当前 v6 环境给出明确报错。
- [ ] 运行 `--check`/dry-run 验证。
- [ ] Commit。

### Task 8: Q10 常量决策包

**Files:**
- Create: `docs/decisions/2026-10-19-Q10-constants.md`

**Interfaces:**
- Produces: 一页批准请求，列出现值、来源、影响、建议；不改代码常量。

- [ ] 从 `contracts/参数冻结表.md` 汇总。
- [ ] 标注需用户批准/需实测两类。
- [ ] Commit。

### Task 9: 文档状态纠偏（A2/C4）

**Files:**
- Modify: `README.md`
- Modify: `ESP32-buddy_文档索引.md`
- Modify: `ESP32-buddy_测试用例与追踪矩阵.md`
- Modify: `contracts/README.md`

**Interfaces:**
- Produces: 文档状态与 evidence 一致；测试计数 589/69；H 未跑项保持 `NOT_RUN`。

- [ ] 修 README 阶段描述、索引交接、矩阵矛盾、契约冻结表。
- [ ] 跑链接/计数检查。
- [ ] Commit。

### Task 10: 全量验证

- [ ] `bash tests/run.sh` → 589/0（或新增后的新总数）
- [ ] `./host/run-tests.sh` → 全绿
- [ ] `bash tools/sim.sh` → 生成全部场景
- [ ] `bash tools/acceptance/run_host_checks.sh` → 通过
- [ ] `git status` 干净；`git log` 本分支提交可读
