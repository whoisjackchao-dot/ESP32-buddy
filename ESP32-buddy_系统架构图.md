# ESP32-buddy 系统架构图

> 版本：v1.4.0（2026-10-25）　·　设备：M5StickS3（ESP32-S3，135×240，ES8311，8MB Flash/PSRAM）
> 说明：本图由仓库代码还原（`firmware/components/pet/`、`host/`、`.opencode/`）。

## 1. 系统总览

```mermaid
flowchart LR
  subgraph DEV["设备 M5StickS3（ESP32-S3）"]
    direction TB
    APP["pet_engine（7fps 渲染/调度）"]
    FEAT["pet_state / pet_render / pet_pomo / pet_leisure / pet_menu / pet_voice / pet_ptt"]
    IO["pet_keys(KEY1/KEY2) · pet_audio(ES8311 I2S 全双工) · pet_hal_sticks3(LCD/PM1/L3B)"]
    NET["pet_net（WiFi STA/AP · SNTP · TCP:8766）<br/>pet_prov（AP 配网 HTTP+DNS）"]
    RX["pet_rx（统一事件队列 + 500ms 去重）<br/>pet_link（NDJSON 解析）"]
    NVS[("NVS：petpomo / petnet / petasr")]
    APP --> FEAT --> IO
    APP --> RX
    RX --> NET
    FEAT --> NVS
  end

  subgraph HOST["主机（Windows + WSL）"]
    direction TB
    SUP["pet_supervisor（退避重启 1→30s · 状态 JSON）"]
    POLL["oc_poll（OpenCode DB 轮询 → NDJSON）"]
    BR["file_to_serial（串口桥）<br/>事件→COM5 · 读控制台 · 剪贴板+Ctrl+V/Enter"]
    HTTP["pet_bridge（HTTP :8765 → 串口，手动/调试）"]
    WK["win_focus / win_keys（Win32 焦点与按键）"]
    SUP --> POLL
    SUP --> BR
    BR --> WK
  end

  OC[("OpenCode<br/>SQLite DB + TUI")]
  Z["智谱 ASR<br/>HTTPS open.bigmodel.cn"]
  PHONE["手机<br/>配网页 192.168.4.1"]

  POLL -- "只读 SQLite" --> OC
  POLL -- "NDJSON" --> BR
  BR -- "USB-Serial-JTAG（COM5）" --> RX
  POLL -- "WiFi TCP :8766" --> NET
  APP -- "HTTPS POST WAV" --> Z
  Z -- "识别文本" --> APP
  PHONE -- "WiFi 热点 + 网页" --> NET
```

## 2. 设备内部分层

```mermaid
flowchart TB
  subgraph L4["应用/编排"]
    ENGINE["pet_engine：帧循环 7fps · 模式 DUTY/POMO · 菜单 · 小剧 · 背光 · 语音覆盖层"]
  end
  subgraph L3["功能"]
    STATE["pet_state：6 态 + 卡栈（≤3） + 失联判定"]
    POMO["pet_pomo：番茄钟 + 当日统计（NVS）"]
    LEI["pet_leisure：DUTY/BORED/SLEEPY/LIGHTS_OUT"]
    MENU["pet_menu：MUTE / STATUS / WIFI: SETUP / BACK: WINDOW"]
    PTT["pet_ptt：PTT → WAV → 智谱 ASR → 文本"]
    VOICE["pet_voice：播报 + 去重"]
  end
  subgraph L2["渲染/文本"]
    RENDER["pet_render：6 态/任务卡/圆环/菜单/小剧/紧凑行"]
    TEXT["pet_text：8×8 ASCII + 16×16 CJK（GB2312 全集）"]
  end
  subgraph L1["I/O 与通信"]
    KEYS["pet_keys + pet_button（SHORT/LONG/PRESS/RELEASE）"]
    RX["pet_rx（统一队列 + 500ms 去重）"]
    LINK["pet_link + pet_linebuf（NDJSON）"]
    NET["pet_net / pet_prov / pet_evt_tcp（WiFi/SNTP/AP配网/TCP:8766）"]
    AUDIO["pet_audio（ES8311 DAC/ADC，I2S 全双工 16k）"]
    HAL["pet_hal_sticks3（LCD/SPI、PM1/L3B、I2C）"]
  end
  KEYS --> ENGINE
  ENGINE --> STATE
  ENGINE --> POMO
  ENGINE --> LEI
  ENGINE --> MENU
  ENGINE --> PTT
  ENGINE --> VOICE
  ENGINE --> RENDER
  RENDER --> TEXT
  LINK --> RX
  RX --> STATE
  NET --> RX
  PTT --> AUDIO
  ENGINE --> HAL
```

## 3. 事件流（OpenCode → 设备）

```mermaid
sequenceDiagram
  participant OC as OpenCode
  participant DB as SQLite
  participant PL as oc_poll
  participant BR as file_to_serial
  participant DEV as 设备 pet_rx
  participant ENG as pet_engine

  OC->>DB: 会话/消息写入
  PL->>DB: 只读轮询（1s）
  PL->>BR: NDJSON（file + TCP :8766 双写）
  BR->>DEV: USB（COM5）
  PL-->>DEV: WiFi TCP :8766（并行）
  DEV->>DEV: 队列 + 500ms 去重（USB/WiFi 同行为一条）
  DEV->>ENG: pet_state_apply → 卡片/状态/语音
```

## 4. 语音流（长按 KEY1 → 自动填入 → 短按发送）

```mermaid
sequenceDiagram
  participant U as 用户
  participant BTN as pet_button
  participant PTT as pet_ptt
  participant MIC as ES8311/I2S RX
  participant Z as 智谱 ASR
  participant ENG as pet_engine
  participant BR as file_to_serial
  participant WIN as OpenCode 窗口

  U->>BTN: 按住 KEY1
  BTN->>PTT: PRESS
  PTT->>MIC: 16k 单声道采集（PSRAM ≤30s）
  ENG-->>U: 屏幕「聆听中...」
  U->>BTN: 松开
  BTN->>PTT: RELEASE
  PTT->>Z: HTTPS multipart WAV（Bearer Key）
  ENG-->>U: 屏幕「理解你的意思」
  Z-->>PTT: {"text":"…"}
  PTT-->>ENG: 中文文本（宠物下方）
  PTT->>BR: 控制线 {"cmd":"voice.text","text":…}
  BR->>WIN: 剪贴板 + 聚焦 + Ctrl+V（保持全屏）
  U->>BTN: 短按 KEY1
  BTN->>BR: 控制线 {"cmd":"send"}
  BR->>WIN: Enter（发送）
```

## 5. 配网与启动

```mermaid
flowchart TD
  BOOT["上电"] --> NVSQ{"NVS 有 WiFi 凭据?"}
  NVSQ -- 否 --> AP["开热点 ESP32-Buddy-XXXX<br/>192.168.4.1 + DNS 劫持"]
  AP --> PAGE["手机网页：扫描列表选网 + 密码"]
  PAGE --> SAVE["保存 NVS → 关热点"]
  NVSQ -- 是 --> STA["STA 连接家庭 WiFi"]
  SAVE --> STA
  STA --> IP["拿到 IP → TCP :8766 事件服务 + SNTP"]
  IP --> HOUR["SNTP 同步 → 注入 hour（夜间熄屏）"]
  STA -- "5 次失败 ≈2min" --> AP
  IP --> MENU["菜单 WIFI: SETUP 可随时重配网"]
```

## 6. ASCII 总览（便于终端阅读）

```
┌──────────────────────────── 设备 M5StickS3 (ESP32-S3) ────────────────────────────┐
│  pet_engine (7fps) ── pet_state / pet_pomo / pet_leisure / pet_menu / pet_ptt      │
│      │                    pet_render (135×240) + pet_text (8×8 + 16×16 GB2312)     │
│      ├─ pet_keys/pet_button  KEY1: 长=说话/短=发送   KEY2: 短=番茄钟/长=菜单       │
│      ├─ pet_rx (队列+去重) ← pet_link ← USB-Serial-JTAG / WiFi TCP :8766          │
│      ├─ pet_net (WiFi STA/AP · SNTP · TCP:8766) + pet_prov (AP 配网 HTTP+DNS)      │
│      ├─ pet_audio (ES8311 I2S 全双工 16k：DAC 播报 / ADC 麦克风)                  │
│      └─ NVS: petpomo(统计) / petnet(WiFi) / petasr(ASR Key)                        │
└───────────────┬───────────────────────────────────────────────┬───────────────────┘
                │ USB (COM5)                                    │ WiFi 2.4G
┌───────────────▼──────────────── 主机 (Windows + WSL) ─────────▼──────────────────┐
│ pet_supervisor (退避 1→30s, 状态 JSON)                                            │
│   ├─ oc_poll  ── 只读 SQLite ──► OpenCode DB                                     │
│   └─ file_to_serial  事件→COM5 · 读控制台(IP/voice.text) · 剪贴板+Ctrl+V/Enter    │
│ pet_bridge (HTTP :8765 → 串口, 手动/调试)     win_focus / win_keys (Win32)        │
└───────────────▲───────────────────────────────────────────────────────────────────┘
                │ HTTPS multipart (WAV)
        ┌───────▼────────┐
        │ 智谱 ASR 云端  │ → 识别文本 → 设备 + 剪贴板 → 短按 KEY1 回车
        └────────────────┘
```

## 7. 关键文件映射

| 层 | 文件 |
| --- | --- |
| 设备编排 | `firmware/components/pet/pet_engine.c` |
| 状态/卡栈 | `pet_state.c`、`pet_link.c`、`pet_rx.c` |
| 渲染/文本 | `pet_render.c`、`pet_text.c`、`assets/cjk16.h`、`assets/robot.look` |
| 番茄钟/休闲/菜单 | `pet_pomo.c`、`pet_leisure.c`、`pet_menu.c`、`pet_voice.c` |
| 语音输入 | `pet_ptt.c`、`pet_audio.c`、`pet_button.c`、`pet_keys.c` |
| 通信/配网 | `pet_net.c`、`pet_prov.c`、`pet_evt_tcp.c` |
| 硬件 | `pet_hal_sticks3.c`、`pm1.c`、`es8311/` |
| 主机 | `host/pet_supervisor.py`、`oc_poll.py`、`file_to_serial.py`、`pet_bridge.py`、`win_focus.py`、`win_keys.py` |
| OpenCode | `.opencode/command/esp32-buddy.md`（插件 `.opencode/plugin/voice-draft.ts` 在 1.19.4 不兼容，已停用） |
