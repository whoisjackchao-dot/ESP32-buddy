# ESP32-buddy 文档索引

> **任何会话的第一个动作是读本文件。** 它是指针，不是内容。**超过一屏即失效。**

| 字段 | 内容 |
| --- | --- |
| 当前阶段 | **阶段 3（开发）** |
| 当前节点 | **M1–M7 与 M8 增量 1/1b/2 已实现，并有对应证据**：M1–M5（含 USB-Serial-JTAG 传输与 OpenCode 真源）、M6（按键/回窗口/菜单，真机通过）、M7（逻辑/圆环/NVS，真机目视）、M8（休闲/小剧/背光 + 多卡栈 T）。**M8 多卡设备版式 H 待做；M9 未开工**。host：**C 599/0**、**Python 98/98**；CI 覆盖 gcc/clang、Python、IDF 5.5.4 构建、模拟器。 |
| 本会话目标 | **无设备加固（2026-10-19）**：修复测试基线 + CI + 离线模拟（多卡/菜单/番茄钟/小剧）+ 统一主机守护进程 + 可选 token + M9 host 预检/runbook + 可复现构建/manifest + Q10 决策包 |
| 文档版本 | **基线 v0.19（2026-10-19；无设备加固 + 主机单进程 + CI + M9 预检）** |
| 已冻结 | M1 契约（含缺陷修订）；M2 §7/§8/§9；M3 §5；M4 §6/§8/§9（2b 已实现，OpenCode 真源已验证）；M5 §5；M6 §7/§8/§9；M7 §6/§7；M8 §6/§7/§8 **（各契约常量仍为提案值，待 `Q10` 批准）** |
| 待批准 | ⏳ `Q10`（语义常量终值，现按提案实现；决策包见 `docs/decisions/2026-10-19-Q10-constants.md`）；`Q17` 已决（A：仅授时+保留 BLE） |
| 已发布 | ✅ GitHub 公开仓库 `whoisjackchao-dot/ESP32-buddy`（2026-10-16） |
| 阻塞 | ~~刷写受阻（`B14`）~~ **已解除**；**剩余**：多卡**设备版式 H**、**M9 真机验收**；**暂缓**：`waiting`（用户跳过）、**NTP**（用户暂不做 → 不熄屏）、刷新率数值 |
| 下一步 | ① **多卡真机目视**（紧凑行、宠物交叠、字号；可先用 `sim/cards-3.bmp` 预检）；② **M9**：按 `tools/acceptance/README.md` 跑 G1–G7 + 故障注入 + manifest；③ NTP/waiting 按用户决定暂缓 |

## 文档地图

| 文档 | 内容 |
| --- | --- |
| [需求与决策基线](./ESP32-buddy_需求与决策基线.md) | **§0 顶层产品目标** + R / G / Q / B（含批准记录与边界） |
| [M0–M9 开发计划](./ESP32-buddy_M0-M9开发计划.md) | 里程碑卡片、DoR、冻结时间表、授权、停止条件 |
| [测试用例与追踪矩阵](./ESP32-buddy_测试用例与追踪矩阵.md) | `R → 用例 → M → 证据` 追踪、执行状态 |
| [contracts/](./contracts/README.md) | 契约清单与冻结文档（见下表） |
| [`.opencode/skills/dayu/`](./.opencode/skills/dayu/SKILL.md) | **内置的 dayu 方法论**（MIT，版权属作者本人）：新机器 clone 后即可按 dayu 规范继续开发；含 `references/templates/` |
| [NOTICE](./NOTICE.md) | 第三方素材/许可登记、商标边界、净室声明 |
| [README](./README.md) | 项目简介、环境、仓库结构 |
| [可复现构建](./docs/reproducible-build.md) | IDF 5.5.4 锁定、容器构建、manifest |
| [M9 验收 runbook](./tools/acceptance/README.md) | G1–G7 真机步骤 + host 预检脚本 |
| [Q10 常量决策包](./docs/decisions/2026-10-19-Q10-constants.md) | 语义常量提案值与批准请求 |

### contracts/ 一览

| 契约 | 状态 |
| --- | --- |
| [M1 渲染分层（冻结）](./contracts/M1-渲染分层契约冻结.md) | ✅ 已冻结 |
| [M2 状态机与动画](./contracts/M2-状态机与动画契约清单.md) | 🟡 部分冻结（§7/§8/§9） |
| [M3 竖屏版式](./contracts/M3-竖屏版式契约清单.md) | 🟡 部分冻结（§5 版式+任务卡） |
| [M4 事件入口与 OpenCode 接入](./contracts/M4-事件入口与OpenCode接入契约清单.md) | 🟡 部分冻结（§6/§8/§9 已实现；OpenCode 真源已验证；契约文本待回写） |
| [M5 语音](./contracts/M5-语音契约清单.md) | 🟡 部分冻结（§5） |
| [M6 按键映射](./contracts/M6-按键映射契约清单.md) | 🟡 部分冻结（§7/§8/§9；映射已批准；回窗口方案 B；长按阈值待 Q10） |
| [M7 番茄钟](./contracts/M7-番茄钟契约清单.md) | 🟡 部分冻结（§6/§7） |
| [M8 休闲与多卡栈](./contracts/M8-休闲与多卡栈契约清单.md) | 🟡 部分冻结（§6/§7/§8；多卡设备 H 待） |
| [ADR-01 回窗口实现方式](./contracts/ADR-2026-10-16-01-回窗口实现方式.md) | ✅ 已批准（方案 B） |
| [ADR-02 WiFi 角色](./contracts/ADR-2026-10-16-02-WiFi角色.md) | ✅ 已批准（方案 A：仅授时 + 保留 BLE） |
| [参数冻结表](./contracts/参数冻结表.md) | 📊 汇总（常量待批准） |

## 交接（最后更新：2026-10-19）

### 2026-10-19 无设备加固（本次）

- **测试基线**：`tests/run.sh` 修 `-lm`、`format-truncation` 缓冲、编译失败不中断、可执行位；gcc 15 **599/0**；host Python **98/98**。
- **CI**：`.github/workflows/ci.yml`（gcc/clang、Python 3.11/3.13、`espressif/idf:v5.5.4` 构建、模拟器 artifact）。
- **离线模拟**：`tools/sim.sh` 新增 2/3 卡、菜单、番茄钟、7 小剧 × 2 帧；`sim/MANIFEST.txt` 记录 FNV-1a；`tests/test_pet_layout.c` 锁 3 行卡区边界。
- **故障注入**：控制字符/超大 elapsed 在建线前拒绝；非法 UTF-8、坏 tasks、坏 DB 行、junk 设备线均有回归（`host/tests/test_pet_robustness.py`）。
- **主机产品化**：`host/pet_service.py` 单进程整合 HTTP + OpenCode 轮询 + 串口读/写；队列、写失败重连、可选 `X-Pet-Token`（`host/tests/test_pet_service.py`）。
- **M9 准备**：`tools/acceptance/run_host_checks.sh`、`tools/acceptance/README.md`、`evidence/M9/TEMPLATE.md`、`evidence/M9/2026-10-19-host-acceptance.md`。
- **可复现构建**：`tools/build-firmware.sh`（拒绝非 v5.5.4）、`tools/release-manifest.sh`、`docs/reproducible-build.md`；`NOTICE.md` 修正为自绘立绘并登记 ESP-IDF/pyserial。
- **决策包**：`docs/decisions/2026-10-19-Q10-constants.md`（待用户批准）。
- **未做**：多卡真机 H、M9 G1–G7 H、NTP、`waiting`、跨 WSL/Windows 的部署收敛（统一服务已可单机运行）。

### 2026-10-17 已完成（历史）

- **阶段/节点**：**阶段 3（开发）**；M1（T/D/H ✅）、M2 ✅（T/H）、M3 ✅（T/H）、M4 增量 1+2a+2b ✅（**USB-Serial-JTAG 端到端**；OpenCode 真源 ✅）、M5 ✅（T/H）、**M6 ✅**（按键/回窗口/菜单，H ✅）、**M7 增量 1+2 ✅**（逻辑/圆环/NVS，H ✅）、**M8 增量 1+1b ✅**（休闲/小剧/背光，H ✅）、**M8 多卡栈 T ✅**（host 589/0 + 构建；设备版式待 H）；M9 未开工
- **当时完成**（文档纠偏 + 环境基线 + `Q15` + M7 增量1 + M4 增量2a+2b + **实机刷机/端到端 H**）：
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
- **仍未完成 / 阻塞**：
  - **多卡栈**：T ✅（host 599/0 + 离线版式断言），**设备紧凑版式 H 待**；`sim/cards-3.bmp` 已预检，但不能替代 H。
  - **M9 验收/交付**：host 预检/runbook/manifest 已就绪；**G1–G7 真机 H 未跑**。
  - **暂缓（用户决定）**：`waiting`（P1-2b，跳过）、**NTP 授时**（暂不做 → `hour24=-1` 不熄屏）。
  - 刷新率**数值**、除 OpenCode 外的 Agent 源（`B09`）。
  - **跨 OS 部署**：统一服务已可单机运行；WSL 的 OpenCode DB + Windows 的 COM5 仍需薄桥接/TCP 方案（未做）。
- **下一步（按优先）**：
  1. **多卡真机目视**（紧凑 3 行、宠物交叠、字号）
  2. **M9 验收/交付**：按 `tools/acceptance/README.md` 跑 G1–G7 + 故障注入 + manifest
  3. **Q10 批准**：按 `docs/decisions/2026-10-19-Q10-constants.md` 回写常量
  4. **暂缓**：NTP、`waiting`、多 Agent
- **待用户决定**：
  - ⏳ `Q10` 语义常量终值（现按提案实现；决策包已就绪）
  - **`Q17` 已决**（A：仅授时 + 保留 BLE）
- **不要重复询问**：Q01–Q09、Q11–Q17、Q18 见基线批准记录；**`Q15` 方案 B**、**`Q17` 方案 A**；**Q09 已发布**；**语音资产 = 自合成**；**es8311 = vendored**；**刷机前不备份**
- **已知不可信**：`pet_audio` 的 MCLK 倍数（256）源自既有项目且其自述**未定案**；**WiFi/NTP 未验证**；除 OpenCode 外的 Agent 源未验证；**`B08` 的激活在个别环境下可能失败**；**刷新率/时序无数值**；**`waiting` 未验证**；**多卡紧凑版式未实机核对**；**视图结构体增大（栈上 +~400B）未经真机压力验证**；**本机无 IDF 5.5.4**（CI 容器为准）；**跨 WSL/Windows 部署未收敛**
