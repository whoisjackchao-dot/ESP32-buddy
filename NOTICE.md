# NOTICE —— 第三方素材、许可与商标边界

本文件登记本项目使用的外部素材及其许可义务。**代码许可与素材许可分开记录。**

## 1. 立绘（角色美术）

| 项 | 内容 |
| --- | --- |
| 素材 | 机器人立绘（48×64，4bpp，RGB565 调色板，4 关键帧：Normal / EyesClosed / Happy / Sad） |
| 来源 | 仓库 `second-state/vibebuddy`，`docs/pet.md` / Character pack（`tools/make-look.py` 生成格式） |
| 许可 | **CC BY-SA 4.0**（见上游 `LICENSE-ASSETS`） |
| 义务 | ① **署名**上游；② 若**修改/派生素材**，衍生美术须以 **CC BY-SA 4.0** 发布 |
| 本项目使用方式 | 直接使用上游成品立绘（未修改） |

**署名文案（模板）**：

> 宠物立绘来自 second-state/vibebuddy，依 CC BY-SA 4.0 使用。

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

> 该文件随源码放在 `firmware/components/pet/font8x8_basic.h`，未改动字形数据（仅把声明改为 `static const uint8_t` 并加 `stdint.h`）。

## 4. 设计借鉴声明

本项目的宠物状态机、事件模型（`task.start` / `task.done` / `task.error` / `agent.input_required` / `agent.idle` / `agent.blocked`）与显示分层思路，借鉴自 vibebuddy 的**设计文档与行为规格**。

**未复制上游源代码。** 上游固件为 Rust（GPL-3.0-or-later），本项目为 C/ESP-IDF 净室实现，故本项目代码不受其 GPL 约束。

> 边界：若将来本项目**复制**上游任何源文件，则该部分须按 GPL-3.0-or-later 处理，且不得与上述"净室"声明并存——届时应开 ADR 并回报用户。

## 5. 待补

| 项 | 状态 |
| --- | --- |
| 代码 LICENSE 全文（MIT） | ✅ 已落盘（2026-10-16）；**署名行默认填 `ESP32-buddy contributors`，请确认或改为你的名字/组织** |
| 上游 CC BY-SA 4.0 全文引用位置 | ⏳ M1 前补 |
| 其他第三方组件（LVGL、ES8311 驱动等） | ⏳ M1 起逐项登记 |
