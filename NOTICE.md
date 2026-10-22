# NOTICE —— 第三方素材、许可与商标边界

本文件登记本项目使用的外部素材及其许可义务。**代码许可与素材许可分开记录。**
机器可校验的哈希见 `acceptance/manifest.json`（`python3 tools/make-manifest.py --check`）。

## 1. 立绘（角色美术）——**本项目原创**

| 项 | 内容 |
| --- | --- |
| 素材 | `firmware/components/pet/assets/robot.look`（48×64，4bpp，**12 帧 = 6 态 × 2 毛发相位**） |
| 来源 | **本项目原创**，由 `tools/make-look.py` **程序化生成**（"毛绒头像"：无身体/四肢/耳朵，顶部一撮会摆动的毛） |
| 风格参考 | 受 Muse / Jolly 的**风格启发**；**未复制任何上游素材或像素数据** |
| 许可 | **随本项目代码以 MIT 发布**；本仓库**不涉及 CC BY-SA 美术** |
| 再生成 | `python3 tools/make-look.py > firmware/components/pet/assets/robot.look` |
| 当前哈希 | `63b2b93f…`（全值见 `acceptance/manifest.json`） |

> **历史说明（2026-10-17 变更）**：早期设计曾计划直接使用 `second-state/vibebuddy` 的立绘（CC BY-SA 4.0）；实测发现上游所谓"机器人"是**代码绘制、仓库内无成品资产**，遂改为**自绘**（决策 `Q18`）。本文件此前的 CC BY-SA 条款**已作废**。

## 2. 商标边界（重要）

上游明确：其两份许可证（GPL-3.0-or-later 与 CC BY-SA 4.0）**均不授予** "VibeBuddy" 名称或氛围小助手角色的**商标权利**。

因此本项目：

- **不使用** "VibeBuddy" 作为项目名、产品名或品牌；
- **不暗示** 与上游存在隶属、赞助或官方关联；
- 项目定名 **ESP32-buddy**。

## 3. 其他第三方资源

| 资源 | 来源 | 许可 | 用途 |
| --- | --- | --- | --- |
| `firmware/components/pet/font8x8_basic.h` | Daniel Hepper（基于 Marcel Sondaar / IBM VGA 公有领域点阵字体） | **Public Domain** | 任务卡与菜单文本（8×8 ASCII） |
| `firmware/components/pet/assets/{done,failed,input}.pcm` | `tools/voice-tts.sh` 用 **edge-tts** 合成，音色 `zh-CN-XiaoyiNeural` | edge-tts 代码 **MIT**；**合成音频的再分发需自行确认服务条款** | M5 三段短语音（16 kHz / 16-bit / stereo 槽位） |
| `firmware/components/es8311/` | **vendored**：`espressif/esp-bsp` → `components/es8311`，版本 `1.0.0~1`，commit `4dabefca` | **Apache-2.0**（原样保留 `LICENSE` 与 `CHECKSUMS.json`） | ES8311 音频编解码器驱动 |
| `.opencode/skills/dayu/` | 本项目作者内置的方法论技能 | **MIT**（`LICENSE` 随附） | 项目流程规范 |

> 字体文件未改动字形数据（仅把声明改为 `static const uint8_t` 并加 `stdint.h`）。

## 4. 设计借鉴声明

本项目的宠物状态机、事件模型（`task.start` / `task.done` / `task.error` / `agent.input_required` / `agent.idle` / `agent.blocked`）与显示分层思路，借鉴自 vibebuddy 的**设计文档与行为规格**。

**未复制上游源代码。** 上游固件为 Rust（GPL-3.0-or-later），本项目为 C/ESP-IDF 净室实现，故本项目代码不受其 GPL 约束。

> 边界：若将来本项目**复制**上游任何源文件，则该部分须按 GPL-3.0-or-later 处理，且不得与上述"净室"声明并存——届时应开 ADR 并回报用户。

## 5. 许可状态汇总

| 项 | 状态 |
| --- | --- |
| 代码 LICENSE 全文（MIT） | ✅ 已落盘（`LICENSE`，署名行 `ESP32-buddy contributors`） |
| 立绘许可 | ✅ **原创，MIT**（不再是 CC BY-SA） |
| 上游 CC BY-SA 全文引用 | ✅ **不适用**（未使用上游美术） |
| 其他第三方组件 | ✅ 已登记（es8311 / font8x8 / edge-tts / dayu） |
| 机器可校验哈希 | ✅ `acceptance/manifest.json`（`legal` 组 + `third_party` 组） |
