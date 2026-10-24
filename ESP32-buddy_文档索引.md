# ESP32-buddy 文档索引

> **任何会话的第一个动作是读本文件。** 它是指针，不是内容。**超过一屏即失效。**

| 字段 | 内容 |
| --- | --- |
| 当前阶段 | **阶段 6（交付）已完成**：M1–M9 全部门槛 PASS（含用户批准的缩减口径）；已发布 **v1.0.0 / v1.1.0 / v1.2.0**；**v1.3.0 已发布** |
| 当前节点 | **M1–M8 ✅ + M9 全门槛 ✅ + M11（WiFi/NTP）✅ + M12（热点配网/菜单配网/SNTP/IP）✅ + 入口守护 ✅ + `/esp32-buddy` ✅**；真机：G1 20/20、G2 各1组、G3 124ms、G4 15.0–15.06s、G5 120min、G6 HTTP 54ms、G7 20/20+对照、F1–F8、多卡 3 行、热点配网（扫列表）、菜单配网、番茄钟页 IP |
| 本会话目标 | ✅ 已完成：M9 验收真机 + 多卡 H + B16/B17 缺陷 + WiFi/NTP + 热点 Web 配网（扫描列表）+ 菜单配网 + 入口守护 + `/esp32-buddy` + 番茄钟页 IP + 发行 v1.0–v1.3 |
| 文档版本 | **v0.20（2026-10-24；M9 全门槛 + WiFi 热点配网 + 入口守护 + v1.3.0）** |
| 已冻结 | M1 契约（含缺陷修订）；M2 §7 状态机 + §8 渲染半段 + §9 动画节拍；M3 §5 版式/任务卡；M4 §6 增量1 事件线格式 + §8 增量2a 主机入口核心 + §9 增量2b 传输；M5 §5 增量1 语音逻辑/资产；M7 §6 增量1 番茄钟逻辑 + §7 增量2 番茄钟视图/落盘；M8 §6/§7/§8；**M9 测量契约** |
| 待批准 | ✅ **无**（`Q10` 2026-10-23 按提案值批准；`Q19=A` 2026-10-24；`Q20/Q21` 定为仪器法；`Q17` 已决 A） |
| 已发布 | ✅ 仓库公开（2026-10-16）；**Releases**：`v1.0.0`（M9 验收）/ `v1.1.0`（WiFi/NTP + 守护 + `/esp32-buddy`）/ `v1.2.0`（热点配网 + 菜单配网 + SNTP）→ https://github.com/whoisjackchao-dot/ESP32-buddy/releases ；**`v1.3.0`**（番茄钟页 IP）→ https://github.com/whoisjackchao-dot/ESP32-buddy/releases/tag/v1.3.0 |
| 阻塞 | ✅ **无**（B14 已解除；B16/B17 已修；B11 配网已落地）；**仅剩**：`v1.3.0` 已推送并发布；`B09` 多 Agent 源属范围外；未验证项见下 |
| 下一步 | ① 网络恢复后 `git push --tags` + 建 `v1.3.0` Release；② 可选复验：DNS 自动弹窗 / STA 失败 ≈2 min 回落；③ 后续增量（B09 多 Agent、RLCD）另立项 |

## 文档地图

| 文档 | 内容 |
| --- | --- |
| [需求与决策基线](./ESP32-buddy_需求与决策基线.md) | **§0 顶层产品目标** + R / G / Q / B（含批准记录与边界） |
| [M0–M9 开发计划](./ESP32-buddy_M0-M9开发计划.md) | 里程碑卡片、DoR、冻结时间表、授权、停止条件 |
| [测试用例与追踪矩阵](./ESP32-buddy_测试用例与追踪矩阵.md) | `R → 用例 → M → 证据` 追踪、执行状态 |
| [contracts/](./contracts/README.md) | 契约清单与冻结文档（见下表） |
| [acceptance/](./acceptance/README.md) | **M9 验收包**：G1–G7 执行清单、manifest、许可登记 |
| [`.opencode/skills/dayu/`](./.opencode/skills/dayu/SKILL.md) | **内置的 dayu 方法论**（MIT，版权属作者本人）：新机器 clone 后即可按 dayu 规范继续开发；含 `references/templates/` |
| [NOTICE](./NOTICE.md) | 许可与署名（**立绘为原创 MIT**）、商标边界、净室声明 |
| [README](./README.md) | 项目简介、环境、仓库结构 |

### contracts/ 一览

| 契约 | 状态 |
| --- | --- |
| [M1 渲染分层（冻结）](./contracts/M1-渲染分层契约冻结.md) | ✅ 已冻结 |
| [M2 状态机与动画](./contracts/M2-状态机与动画契约清单.md) | 🟡 部分冻结（§7/§8/§9） |
| [M3 竖屏版式](./contracts/M3-竖屏版式契约清单.md) | 🟡 部分冻结（§5 版式+任务卡） |
| [M4 事件入口与 OpenCode 接入](./contracts/M4-事件入口与OpenCode接入契约清单.md) | 🟡 部分冻结（§6 增量1 + §8 增量2a）；增量2b 阻塞（传输/OpenCode） |
| [M5 语音](./contracts/M5-语音契约清单.md) | 🟡 部分冻结（§5 增量1） |
| [M6 按键映射](./contracts/M6-按键映射契约清单.md) | ⏳ 清单就绪（映射已批准；回窗口已决→B；阈值待定） |
| [M7 番茄钟](./contracts/M7-番茄钟契约清单.md) | 🟡 部分冻结（§6 增量1） |
| [M8 休闲与多卡栈](./contracts/M8-休闲与多卡栈契约清单.md) | 🟡 部分冻结（§6/§7/§8） |
| [ADR-01 回窗口实现方式](./contracts/ADR-2026-10-16-01-回窗口实现方式.md) | ✅ 已批准（方案 B） |
| [ADR-02 WiFi 角色](./contracts/ADR-2026-10-16-02-WiFi角色.md) | ✅ 已批准（方案 A：仅授时 + 保留 BLE） |
| [参数冻结表](./contracts/参数冻结表.md) | 📊 汇总（常量待批准） |

## 交接（最后更新：2026-10-24）

- **阶段/节点**：**阶段 6（交付）**；M1–M9 ✅（G1–G7 + F1–F8 真机）；M11 WiFi/NTP ✅、M12 热点配网/菜单配网/SNTP/IP ✅、入口守护 ✅、`/esp32-buddy` ✅；已发布 v1.0–v1.2，v1.3.0 待推
- **本次（2026-10-22）完成**：
  1. **修正仓库漂移**：本地基于 10-16 快照，远端已领先 17 个提交（M4/M6/M7/M8 + `host/` + OpenCode 真源）。重复的 M7 提交存为本地分支 `backup/local-m7-20261022`，`main` 对齐远端。
  2. **修 host 测试在 gcc 15.2 下不可跑**（3 处）：`row[24]`→`[48]`、紧凑卡行 `+16`→`+32`（`-Wformat-truncation`）、`tests/run.sh` 全部补 `-lm`。结果 **589 checks / 0 failed**。
  3. **M9 验收包**：`acceptance/G1-G7-验收执行清单.md`（跑法/判法/记录法 + 故障注入表）、`acceptance/许可证登记.md`、`acceptance/README.md`。
  4. **manifest 工具**：`tools/make-manifest.py` 生成/校验 `acceptance/manifest.json`（**149 文件 / ≈1.37 MB**，8 组，`--check` 幂等）。
  5. **门槛预检（无需设备）**：`tools/gate-host-smoke.py` → **G6-host 20/20**、**故障 7/7**（sink 不被污染）。
  6. **入口可启动**：`host/pet_entry_server.py` 补 CLI（`--host/--port`）+ stdout 逐行 flush，使 G6 的「任意本机脚本 POST」真正可跑。
  7. **修正许可事实错误**：`NOTICE.md`/`README.md` 原称立绘来自上游 CC BY-SA（实为 `tools/make-look.py` **原创**）→ 改为 MIT；README 状态与结构同步更新。
  8. 登记 `Q19`–`Q21`（G2 口径 / G4 起算点 / 1s 测法）到基线 §8 与执行清单 §6。
  9. **交付完整性（第二轮，host-only）**：manifest 加 `--build-dir`，登记三个 `.bin` 的偏移/字节/sha256 + chip/target/IDF 版本；`F6-host` 洪泛 200 事件 × 8 线程 **0 丢弃**；新增 `host/pet_bridge.py` **单进程桥**（入口+串口，写失败重连一次、仍失败丢弃并记账）→ `evidence/M9/2026-10-22-交付完整性-T.md`
  10. 记录两处事实偏差：文档写 IDF **v5.5.4**、实际 **v5.5.5**；入口对"未送达"仍回 **200**（G6 必须看屏，不能只看状态码）。
  11. **修三个可交付性缺陷**：manifest 曾登记 `.gitignore` 排除的 `flash/*.bin`（新 clone 会误报缺文件）→ 只登记仓库内文件、产物走 `build` 段；9 个脚本索引模式是 `100644`（新 clone `./tests/run.sh` 直接 Permission denied）→ 补 `+x`；IDF 版本标注错误 → 从 `esp_idf_version.h` 解析。
  12. **新 clone 端到端实测通过**：`git clone` → C **589/0**、Python **77 OK**、预检 **3/3**、`manifest --check` 一致（无需任何手工修补）。
- **上一轮完成（2026-10-16/17）**：
  1. 新机器环境核查：无 `cc`/无免密 sudo/无 ESP-IDF（`~/esp` 空）/无旧项目（`sticks3-opencode` 空）→ 登记缺口 `B15`
  2. 用**用户态 `zig cc 0.14.1`** 复现 host 单测（后并入 M7）：后随 M4 2b 增至 **277 checks / 0 failed** → `evidence/2026-10-16-新机器环境基线.md`
  3. 修正索引/README/计划/契约的状态漂移（README 阶段、M2 idle、`Q09` 已发布、M2–M5 契约冻结态、重复交接块）
  4. **拍定 `Q15` = 方案 B**（目标 = 承载 OpenCode 的终端窗口）；`ADR-01` 转"已批准" → 基线 §4.2 + M6 契约 §4
  5. **`B08` spike（限范围）**：目标窗口可稳定识别（`WindowsTerminal` / `CASCADIA_HOSTING_WINDOW_CLASS` / `OC | …`）；裸 `SetForegroundWindow` **不可靠**（实测）→ `evidence/B08-回窗口可行性-spike.md`
  6. **M7 增量 1**：冻结契约 §6；实现 `pet_pomo`（计时状态机 + 当日统计，host 可测）；`POMO-01..12`；RED `63/40` → GREEN `64/0` → `evidence/M7/2026-10-16-番茄钟逻辑-T.md`
  7. **M4 增量 2a**：冻结契约 §8；实现**主机侧入口核心**（Python/stdlib，loopback，非法输入不广播）；`E2E-02`/`E2E-03`/`SEC-01`；GREEN 23/OK → `evidence/M4/2026-10-16-主机入口核心-T.md`
  8. **实机刷机（H）**：发现 **Windows 侧 IDF v5.5.4** → 构建成功；Windows esptool 刷 **COM5**（3 镜像 hash 校验通过）；`DISPLAY READY M1`/`AUDIO READY`/`PIXEL CARD READY` + 心跳；**目视：蓝色机器人、约 15 s 灰化（失联）**（**M1 H ✅**）→ `evidence/2026-10-17-实机刷机-H.md`
  9. **M4 增量 2b（传输）+ 端到端 H**：冻结 §9（**USB-Serial-JTAG**，非 BLE）；设备 `pet_rx`/`pet_linebuf` + 主机 `pet_transport`/`pet_sender`；C **277/0**、host **37/OK**；刷机后**脚本发事件 → 6 态依次 / 任务卡正确 / 三句语音听到**（**M2/M3/M5 的 H ✅**）→ `evidence/M4/2026-10-17-传输端到端-H.md`
  10. **M7 增量 2**：冻结 §7；`pet_render_ring`/`pet_render_pomo` + `pet_pomo_encode/decode_stats`（C **324/0**）；`pet_pomo_store`(NVS) + `pet_engine_set_mode` 接入固件（修 `pet_pomo.c` 漏加入 CMake SRCS）并刷机**启动无回归**；**圆环真机目视 4/4**（demo 变体；验后已刷回普通固件）→ `evidence/M7/2026-10-17-番茄钟视图与持久化-{T,D}.md`
  11. **M6 增量 1**：冻结 §7；`pet_button`（短/长按状态机）+ `pet_keymap`（Q11 映射）+ `pet_keys`（GPIO11/12，20 ms 采样）；C **370/0**；刷机后**真机：`KEY2` 切 值班↔番茄钟、`KEY1` 开始/暂停/继续，无误触发**（用户"都正常"）→ `evidence/M6/2026-10-17-按键-{逻辑-T,H}.md`
  12. **M6 增量 2（回窗口 B08）**：冻结 §8；设备 `pet_engine_request_back_window()` 写控制线 `{"v":1,"cmd":"back_window"}`；主机 `pet_devio`+`pet_daemon`+`win_focus`（host **55/OK**）；**真机端到端：KEY1 → 终端回到前台**（用户"可以了"；激活 5/5）。修 4 缺陷（`__main__`缺失、`time`未导入、`WNDENUMPROC`不存在、前台锁→ALT解锁）→ `evidence/M6/2026-10-17-回窗口-H.md`
  13. **P0 收尾**：`tools/host-cc.sh`（host 370/0，B15 收敛）；**修"静音不生效"**（引擎未查 `pet_voice_should_play`）；真机验证 **AUD-06（播放 fps 不变）**、**静音无声/恢复有声**、**NVS 统计跨刷写+复位保留（done=13/focus_s=195）**、**帧率 fps=7** → `evidence/2026-10-17-P0收尾-验证.md`
  14. **M6 增量 3（菜单）**：冻结 §9（仅静音/状态）；`pet_menu` + `pet_render_menu` + `pet_engine_key`（菜单优先）；C **392/0**；**真机：开/关、选中、静音切换、状态只读均正常**（用户"都正常"）→ `evidence/M6/2026-10-17-菜单-{T,H}.md`
  15. **立绘 6 态升级（Muse 启发原创）**：4→**12 帧**（6 态 × 2 毛发相位）；越改越像梨（上窄下宽、下巴圆润、修长）；头顶一撮毛**~1s 左右摆**；真机确认**"合格了"** → `evidence/M1/2026-10-17-立绘6态升级-D.md`
  16. **卡片 UI**：字号**短行 2× / 长行 1×**、**ASCII-only 左对齐**（跳过 UTF-8 不占格）、时长固定 1×、项目字色调浅、卡高 58→64；真机逐轮确认 → `evidence/M3/2026-10-17-卡片字号与英文显示-D.md`
  17. **P1-2a OpenCode 真源**：SQLite 只读轮询（busy/idle/done/error）+ 跨 WSL/Windows 桥接（UNC 直读失败）；**真实 OpenCode → 设备 WORKING→DONE+语音**（用户确认）→ `evidence/M4/2026-10-17-OpenCode{真源-spike,轮询器-T,端到端-H}.md`
  18. **M8 增量 1**：冻结 §6；`pet_leisure`（DUTY→BORED→SLEEPY→LIGHTS_OUT + 小剧调度，时间注入）；C **470/0**；设备侧（小剧美术/背光）与 NTP 未做 → `evidence/M8/2026-10-17-休闲状态机-T.md`
  19. **M8 增量 1b（小剧+背光）**：`pet_render_skit`（7 种：巡逻/踢球/看书/星光/躲猫猫/吓醒/梦话）+ 引擎接入 + `LIGHTS_OUT` 关背光 + **按键唤醒**；C **485/0**；**demo 阈值真机：小剧可见、背光灭、按键唤醒**（用户"1-3 都正常了"）→ `evidence/M8/2026-10-17-小剧与背光-H.md`
  20. **M8 增量 2（多卡栈）**：冻结 §8（`B10=A` 到达时间）；`pet_link` 解析 `tasks[]`（≤3）、`pet_state` 栈（同名更新置顶 / 第 4 张丢最旧）+ `set_cards` 整栈替换、`pet_render` 紧凑 3 行、`pet_rx` 接线；C **589/0**、**ESP-IDF 构建通过**；**设备版式待 H** → `evidence/M8/2026-10-17-多卡栈-T.md`；另：加密 git 凭据（GPG helper，一次性 PAT 不入盘）
  21. **M11 WiFi/NTP + 入口守护 + `/esp32-buddy`**：`pet_net`（STA/AP 状态机 + SNTP → 运行时 hour24）；`pet_supervisor`（退避重启 1→30 s、状态 JSON、停止文件清理）；`tools/esp32-buddy-{up,down,status}.sh`；OpenCode 项目命令 `/esp32-buddy` → `evidence/2026-10-24-WiFi与NTP-设备.md`、`evidence/2026-10-24-入口守护与命令-T.md`
  22. **热点 Web 配网（推翻一次性写入）**：无凭据/菜单触发 → `ESP32-Buddy-XXXX` 开放热点 + `192.168.4.1` 网页 + DNS 劫持；页面**扫描列表选网**（`GET /scan`，APSTA）→ 点选 + 密码 → STA；真机 `ip=192.168.31.198`、`sntp hour=21`
  23. **菜单「配网模式」**：长按 KEY2 → 3 项（MUTE/STATUS/WIFI: SETUP）→ 确认重开热点（M6 §9 变更记录）
  24. **缺陷**：修 `esp_wifi_start` 不自动连接（补 `esp_wifi_connect`）；守护 down 残留（停止文件）；harness 前缀解析；CMake `-DPOMO_*` 转发
  25. **发行**：v1.0.0（M9）→ v1.1.0（WiFi/NTP+守护）→ v1.2.0（热点配网+菜单+SNTP）→ **v1.3.0（番茄钟页 IP，待推）**
- **本次未完成 / 阻塞**：
  - **`v1.3.0`**：✅ 已推送 tag + Release（4 资产）
  - **未验证（如实）**：DNS 劫持自动弹窗、STA 密码错误 ≈2 min 回落热点、扫描期间手机掉线压测、栈/内存水位
  - **范围外**：`B09` 多 Agent 源（除 OpenCode）、`RLCD-4.2`、刷新率数值
- **下一步（按优先）**：
  1. 网络恢复 → `git push origin main --tags` + 建 `v1.3.0` Release（4 资产）
  2. 可选真机复验：DNS 自动弹窗 / STA 失败回落 / 扫描并发
  3. 新需求（多 Agent 等）按新里程碑立项
- **不要重复询问**：Q01–Q09、Q11–Q17、Q18 见基线批准记录；**`Q15` 方案 B**、**`Q17` 方案 A**；**Q09 已发布**；**语音资产 = 自合成**；**es8311 = vendored**；**刷机前不备份**
- **已知不可信**：`pet_audio` 的 MCLK 倍数（256）源自既有项目且其自述**未定案**；**DNS 劫持自动弹窗/STA 失败回落/扫描并发未验**；除 OpenCode 外的 Agent 源未验证；**`zig cc` 环境**为临时（`/tmp`）且告警集与 gcc 未必逐条一致；**`B08` 的激活在个别环境下可能失败**；**刷新率/时序无数值**；**OpenCode 端到端用的是两进程桥接脚手架**（生产化未收敛）；**`waiting` 未验证**；**视图结构体增大（栈上 +~400B）未经真机压力验证**；**合成语音再分发条款未确认**；**刷机副本需手动同步（B16，`tools/sync-fw.sh`）**
- **会话纪律（2026-10-22 追加，教训）**：开工第一步必须 `git fetch` 并**同时核对 ahead 与 behind 两个方向**（`git status -sb` 或 `git rev-list --count HEAD..origin/main`）。本会话曾只查 forward，导致在 10-16 的旧快照上**重复实现已完成的 M7 番茄钟**；发现后把重复提交存为本地分支 `backup/local-m7-20261022` 并 reset 到远端。另：远端作者用 `zig cc`，本机用 gcc 15.2 时曾因 `-Werror=format-truncation` 与缺 `-lm` 无法跑 host 测试，已在 `96e1bce` 修正。
