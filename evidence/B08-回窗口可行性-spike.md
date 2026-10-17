# evidence/B08 —— "回到 Agent 窗口"可行性 spike

> 日期：2026-10-16（新机器会话）；类别：**D**（诊断/可行性 spike）；关联：`Q15`、`ADR-2026-10-16-01`、`BTN-05`、`M6`、缺口 `B08`
> 授权：用户在拍定 `Q15`（方案 B）时**同时授权**本限范围 spike。
> 结论范围：**partially 实测**。已实测：目标窗口识别、helper 提权状态、`SetForegroundWindow` 可靠性。**未**实测：设备按键 → host helper → 激活的**端到端**链路（本机无设备、无 host helper）。

## 1. 假设

在 Windows 上，一个**非提权**的主机 helper（后台进程）能够**稳定地**把"承载 OpenCode 的终端窗口"带到前台。

## 2. 环境（本机实测）

| 项 | 值 |
| --- | --- |
| 宿主 | WSL2（Ubuntu 26.04），`DESKTOP-F7NTBB2`；Windows 互操作可用（`/mnt/c`、`powershell.exe`、`pwsh.exe`） |
| Agent | OpenCode 以**终端 TUI** 运行于 **Windows Terminal** |
| 目标窗口 | `WindowsTerminal`（PID 2724），窗口类 `CASCADIA_HOSTING_WINDOW_CLASS`，标题 `OC | <会话名>` |
| helper 侧 | Windows PowerShell **非提权**（`IsInRole(Administrator)=False`） |

## 3. 判定方法

用 `powershell.exe`（经 WSL 互操作）调用 Win32：`GetForegroundWindow` / `SetForegroundWindow` / `GetClassName` / `GetWindowThreadProcessId` / `AttachThreadInput` / `ShowWindow`。观测：返回值 + `GetForegroundWindow` 是否真的改变。

**终止条件**：得到"稳定可靠 / 不可靠"的判定，或确认本机无法给出判定而转为可执行复测协议。

## 4. 结果（本次实测）

| # | 观测 | 结果 |
| --- | --- | --- |
| R1 | 目标识别 | ✅ **稳定**：`GetForegroundWindow` 类名 = `CASCADIA_HOSTING_WINDOW_CLASS`；标题 `OC \| …`；`(Get-Process WindowsTerminal).MainWindowHandle` 可读（=131790） |
| R2 | helper 提权 | ✅ **非提权**（`IsInRole(Administrator)=False`） |
| R3 | 裸 `SetForegroundWindow`（后台非前台进程） | ⚠️ **不一致**：① 指向 Chrome 返回 `True` 且焦点确实转移；② **紧接着**指向 WT 返回 `False` 且焦点**未**转移（被前台锁拒绝）；③ 另一次从"非 WT 前台"指向 WT 返回 `True` 且成功。→ **不可依赖裸调用** |
| R4 | `AttachThreadInput` + `ShowWindow(SW_RESTORE)` 组合 | ❓ **未得出结论**（该分支因 R3③ 已成功而未触发；需专门复测） |
| R5 | 端到端（设备按键→helper→激活） | ⛔ **未测**（无设备、无 helper） |

> **过程说明**：R3 使本机前台焦点短暂转移（WT→Chrome）。测试后前台已回到 WT。焦点操作**打扰了用户桌面**，仅做了必要次数。

## 5. 结论

- **Q15 方案 B 属"条件可行"**：目标识别没有问题；**裸 `SetForegroundWindow` 不可靠**（前台锁），必须在 host helper 内实现**稳健激活例程**。
- **候选稳健例程**（须在 M6 内实现并真机复测，择一或组合）：
  1. `AttachThreadInput` 到当前前台线程 → `ShowWindow(SW_RESTORE)` → `SetForegroundWindow` → 解绑；
  2. `AllowSetForegroundWindow`（仅当前前台进程可授，适用性有限）；
  3. 降级：taskbar 闪烁 `FlashWindowEx`（**总能成功**，提示用户"点这里"）。
- **不改变 `Q15` 决定**：仍为方案 B。若稳健例程真机复测**仍不可靠**，按 `ADR-01` 切换规则**重评（A/C）并回报用户**，不静默禁用按键。

## 6. 证据分级

| 结论 | 分级 |
| --- | --- |
| 目标窗口可稳定识别（类名/标题/句柄） | **本次实测** |
| helper 非提权 | **本次实测** |
| 裸 `SetForegroundWindow` 不可靠 | **本次实测**（3 次不同序，2 成 1 败） |
| `AttachThreadInput` 是否可靠 | **未知**（未测） |
| 端到端可用性 | **未知**（未测） |

## 7. 边界（本 spike 不覆盖）

- 不覆盖**多显示器 / 多虚拟桌面 / 多 Windows Terminal 标签**时的目标消歧（WT 一个进程多标签，窗口标题=活动标签标题）。
- 不覆盖 helper **提权**运行的场景（UIPI 边界）。
- 不覆盖**非 OpenCode** 的 Agent 窗口（`Q12`）。
- 不承诺"任意窗口定位"（`B08`）。

## 8. 后续（M6 内）

1. host helper 内实现稳健激活例程（优先 `AttachThreadInput` 组合，降级 `FlashWindowEx`）。
2. 真机复测协议（重复 ≥10 次，记录成功率；目标窗口处于非活动/最小化/多标签三种状态各若干次）：
   - 步骤：设备 KEY1 短按 → host 收命令 → 调激活例程 → 观测 WT 是否到前台。
   - PASS：非最小化激活 ≥ 9/10；多标签/最小化场景按设计可降级（Flash）。
   - FAIL：< 9/10 或无法恢复 → 触发 `ADR-01` 切换规则重评。
