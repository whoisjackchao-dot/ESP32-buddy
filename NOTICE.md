# NOTICE —— 第三方素材、许可与商标边界

本文件登记本项目使用的外部素材及其许可义务。**代码许可与素材许可分开记录。**

## 1. 立绘（角色美术）

| 项 | 内容 |
| --- | --- |
| 素材 | 机器人立绘（48×64，4bpp，RGB565 调色板，12 帧 = 6 态 × 2 毛发相位） |
| 来源 | **本项目自绘**，由 `tools/make-look.py` 生成；未复制上游美术文件 |
| 许可 | 随本项目代码/素材发布；**不适用**上游 CC BY-SA 4.0 的衍生义务 |
| 设计借鉴 | 仅借鉴 vibebuddy 的“桌面宠物”设计与状态语义（见 §4），未使用其角色素材 |

> 历史说明：`Q05` 曾批准“必要时可用上游 CC BY-SA 立绘”，但 `Q18` 最终选择自绘占位/定型立绘；当前仓库资产为自绘，故不再声明 CC BY-SA 署名义务。若将来引入上游美术，须恢复 §1 的 CC BY-SA 署名与同协议义务。

## 2. 商标边界（重要）

上游明确：其两份许可证（GPL-3.0-or-later 与 CC BY-SA 4.0）**均不授予** "VibeBuddy" 名称或氛围小助手角色的**商标权利**。

因此本项目：

- **不使用** "VibeBuddy" 作为项目名、产品名或品牌；
- **不暗示**与上游存在隶属、赞助或官方关联；
- 项目定名 **ESP32-buddy**。

## 3. 其他第三方资源

| 资源 | 来源 | 许可 | 用途 |
| --- | --- | --- | --- |
| `font8x8_basic.h` | Daniel Hepper（基于 Marcel Sondaar / IBM VGA 公有领域点阵字体） | **Public Domain** | M3 卡片文本绘制（8×8 ASCII） |
| 播报语音 `assets/*.pcm` | `tools/voice-tts.sh` 用 **edge-tts**（微软 Edge 朗读服务）合成，音色 `zh-CN-XiaoyiNeural` | edge-tts 代码为 **MIT**；**合成音频的再分发需自行确认服务条款** | M5 三段短语音 |
| `firmware/components/es8311/` | **vendored**：`espressif/esp-bsp` → `components/es8311`，版本 `1.0.0~1`，commit `4dabefca` | **Apache-2.0**（原样保留 `LICENSE` 与 `CHECKSUMS.json`） | ES8311 音频编解码器驱动（M5 播放） |
| ESP-IDF v5.5.4 | Espressif | **Apache-2.0** | 固件框架/构建（不 vendored，按工具链使用） |
| `pyserial`（可选） | Chris Liechti 等 | **BSD-3-Clause** | host 串口传输；仅在真实串口路径导入 |
| `.opencode/skills/dayu/` | 本项目作者 | **MIT** | 内置方法论（见其 `LICENSE`） |

> 字体文件随源码放在 `firmware/components/pet/font8x8_basic.h`，未改动字形数据（仅把声明改为 `static const uint8_t` 并加 `stdint.h`）。

## 4. 设计借鉴声明

本项目的宠物状态机、事件模型（`task.start` / `task.done` / `task.error` / `agent.input_required` / `agent.idle` / `agent.blocked`）与显示分层思路，借鉴自 vibebuddy 的**设计文档与行为规格**。

**未复制上游源代码。** 上游固件为 Rust（GPL-3.0-or-later），本项目为 C/ESP-IDF 净室实现，故本项目代码不受其 GPL 约束。

> 边界：若将来本项目**复制**上游任何源文件，则该部分须按 GPL-3.0-or-later 处理，且不得与上述"净室"声明并存——届时应开 ADR 并回报用户。

## 5. 登记状态

| 项 | 状态 |
| --- | --- |
| 代码 LICENSE 全文（MIT） | ✅ 已落盘（2026-10-16）；署名行默认 `ESP32-buddy contributors`，可按需改为作者/组织 |
| 立绘许可 | ✅ 自绘；不依赖上游 CC BY-SA（见 §1） |
| ES8311 / font8x8 / edge-tts / ESP-IDF / pyserial / dayu | ✅ 已登记（见 §3） |
| 发布前复核 | ⏳ `tools/release-manifest.sh` 生成清单时一并核对；不自动打 tag/Release |
