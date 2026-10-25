# ESP32-buddy · BLE 配对 + WiFi 双连接 · 可行方案

> 依据：`evidence/2026-10-25-BLE配对与WiFi双连接.md`（官方矩阵：**WiFi STA + BLE Connected = 稳定**；**SOFTAP + BLE = C1 不稳定**）
> 目标：WiFi 正常时走 WiFi；**WiFi 断开时自动切到 BLE 配对连接**，保证事件/语音控制**不断联**。

## 1. 范围

**做**：
- BLE 外设（NimBLE）+ **LE Secure Connections 配对/绑定**
- 一条自定义 GATT 服务：
  - `RX`（write/write-no-rsp）：主机→设备的 **事件行**（沿用 NDJSON，与 WiFi/USB 同格式）
  - `TX`（notify）：设备→主机的 **控制行**（`voice.text` / `send`，与 WiFi :8767、USB 同格式）
- **策略**：WiFi STA 优先；STA 断开 → 开 BLE 广播/可连；STA 恢复 → BLE 断开（避免常态 50% 分时）

**不做**：
- BLE 传语音/ASR（WiFi HTTPS 更合适）
- BLE 配网（与 AP 热点并存 = C1 不稳定）
- 多主机并发（1 个连接足够）

## 2. 架构

```
设备                                   主机 (Windows)
┌─────────────────────────┐            ┌──────────────────────────┐
│ pet_rx 统一队列(已有)    │            │ BLE GATT Client (WinRT)  │
│   ▲ USB / WiFi(:8766)   │  BLE GATT  │   ├─ 配对/绑定            │
│   └ BLE RX (新增)        │◄──────────►│   ├─ 写 RX: 事件行        │
│ pet_engine_send_line     │            │   └─ 订阅 TX: 控制行      │
│   ├ USB (已有)           │            │        ↓                 │
│   ├ WiFi :8767 (已有)    │            │ 剪贴板 + Ctrl+V / Enter   │
│   └ BLE TX notify (新增) │            │                          │
└─────────────────────────┘            └──────────────────────────┘
            ▲ WiFi STA (优先)  ─────────────  pet_bridge :8767 / oc_poll
```

- 三条通道共享同一套行格式与同一批处理函数（`pet_rx_submit` / `voice.text` / `send`），主机侧**去重 500ms**（已有 `LineDedup`）。
- BLE 只在 **STA 断开**时启用（避免 Wi-Fi+B​LE 各占 50% 时间片拖慢 ASR/事件）。

## 3. 分阶段（每阶段可独立验收）

| 阶段 | 内容 | DoD（验收判据） |
| --- | --- | --- |
| **B1 基线** | 启用 `CONFIG_BT_ENABLED=y` + `BT_NIMBLE_ENABLED=y`（BLE-only），只做广播，不动业务 | 固件可编译/刷机；`WiFi STA + BLE 广播` 正常；记录 flash/RAM 增量与 WiFi 吞吐基线 |
| **B2 GATT 通道** | 定义服务（RX write / TX notify）；设备 RX 行 → `pet_rx_submit`；控制行 → notify | host 写一行事件 → 设备状态变化；设备发 `voice.text` → host 收到（notify 校验） |
| **B3 配对/绑定** | LE Secure Connections（先 Just Works）+ 绑定存储；主机 WinRT 配对 | 重启后自动重连（bond 生效）；未配对不能写 RX/TX |
| **B4 切换策略** | STA 断开 → BLE 广播/可连；STA 恢复 → BLE 断开；`host.addr` 逻辑不变 | 拔 WiFi/关路由：≤5s 内 BLE 可连；恢复 WiFi：BLE 断开、WiFi 恢复；**AP 配网时不启 BLE** |
| **B5 端到端** | 双通道并存 + 去重 + 兜底流程 | 关 WiFi：按住 KEY1 说话 → BLE → 自动填入 → 短按发送；开 WiFi：回归 WiFi 路径；无重复动作 |

## 4. 资源与风险

| 项 | 预估/措施 |
| --- | --- |
| Flash | **+150–250 KB**（当前余 ~520 KB，够） |
| RAM | **+30–60 KB**；必要时 `BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL` 放 PSRAM |
| 射频 | Wi-Fi 连接 + BLE 连接 = **各 50% 时间片**（故仅在 WiFi 断开时用 BLE） |
| 吞吐 | BLE 低位速率；够事件（1 行/秒）与控制行；**不传音频** |
| 功耗 | BLE+WiFi 叠加，**不承诺续航**；建议插电或及时充电 |
| 主机 | Windows WinRT GATT + 配对 UX；参考项目 `esp32-s3-m5` 已实现可借鉴 |
| 风险 | ① 共存下丢包/时延（B1 实测基线）② Windows 配对稳定性 ③ 固件体积（B1 检查） |

## 5. 验收（最终）

1. **WiFi 正常**：事件/语音/控制全部走 WiFi（现状），BLE 不广播。
2. **WiFi 断开**：≤5 s 内设备可被主机 BLE 配对连接；按住 KEY1 说话 → 文本自动填入 + 短按发送（BLE 控制行）。
3. **WiFi 恢复**：BLE 断开，WiFi 恢复，无重复动作（主机 500ms 去重）。
4. **回归**：USB 路径、AP 配网、ASR、番茄钟、菜单均不受影响。

## 6. 建议起步
先做 **B1**（只开 NimBLE 广播 + 记录基线），确认 flash/RAM/共存无异常，再决定是否继续 B2–B5。**不建议一步到位**。
