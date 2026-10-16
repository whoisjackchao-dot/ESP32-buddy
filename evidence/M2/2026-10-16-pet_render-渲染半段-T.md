# M2 · pet_render 渲染半段 · T 类 RED→GREEN 记录

> 日期：2026-10-16　类别：**T**（绘制规则，测试先行）
> 结论范围：**host 单测**通过 + **主机预览图**；**不构成设备实机验收**（G1 的实机目视仍待设备）。
> 契约：`contracts/M2-状态机与动画契约清单.md` §8（2026-10-16 冻结：gray / mirror / 头顶标记）

## 证据字段

| 字段 | 内容 |
| --- | --- |
| 需求 ID | R10（6 态）、R23（失联灰化） |
| 契约 ID | M2 §8 |
| 用例 ID | `UI-02`（状态可辨）、`LCD-02`（surface）、`UI-05`（灰化/翻转/标记） |
| 待测组件 | `firmware/components/pet/pet_render.c` |
| 输入 fixture | `firmware/components/pet/assets/robot.look` sha256 `2d341282…216b6`；另用**合成立绘**测 mirror |
| 代码版本 | `pet_render.c` sha256 `fda2cd9a…0ea4e6`；`tests/test_pet_render.c` sha256 `3a0fa9b4…5898f`；生成器 sha256 `d76b5837…ef8d9` |
| 环境 | WSL2 Ubuntu 26.04；`cc` = gcc 15.2.0 |
| 人工/实机证据 | **无**（见 §4） |

## 1. RED

命令 `./tests/run.sh`（`gray`/`mirror`/标记均未实现）：

- 结果 `37 checks, 9 failed`，退出码 1
- 失败断言：
  - `r8 - g8 <= 6 && g8 - r8 <= 6`（灰化未生效）
  - `get_px(&surface, (LOOK_W-1)*2, 0) == 0x07E0`（mirror 未生效）
  - `has_pixel_in(0, oy-20, SW-1, oy-2) == 1`（WORKING / INPUT 头顶无标记）
  - `idle != work`、`idle != input`、`work != input`（态未区分）

## 2. 过程中发现的两个**测试自身缺陷**（已修正）

| 缺陷 | 现象 | 原因 | 修正 |
| --- | --- | --- | --- |
| **弱测试**：`mirror` 测试假绿 | 未实现 mirror 也通过 | 占位立绘**左右对称**，翻转不可辨 | 改用**非对称合成立绘**（单像素在角上），翻转后位置可判定 |
| **生成器错误**：颜色值当索引 | 资产索引出现 13/9，EAR 缺失 | `make-look.py` 把 RGB565 值（`0x8DDD`）写进像素，打包 `&0x0F` 静默截断 | 改用**调色板索引**常量，并在打包处加**断言** `0 <= v < 16` |

> 第二条是本次最有价值的发现：**没有断言保护的位截断会静默产出错误资产**，而后果直到测试读到"第一个不透明像素"时才暴露。

## 3. GREEN

`./tests/run.sh` → `test_pet_render` **37/0**、`test_pet_state` **39/0**，退出码 **0**；重复运行一致。

覆盖的新行为：

| 用例 | 断言要点 |
| --- | --- |
| 灰化 | 灰化后不透明像素的三通道展开值**相等**（容忍 5/6/5 量化，±6） |
| 翻转 | 非对称立绘的 (0,0) 像素在 `mirror=true` 时出现在 `(LOOK_W-1,0)`，原位置未被绘制 |
| 头顶标记 | IDLE 头顶区间**无**像素；WORKING / INPUT_REQUIRED 该区间**有**像素 |
| 态可辨（G1 的 host 代理） | 6 态渲染结果的 FNV-1a 摘要**两两不同**（idle/work/input/done/failed/offline） |

## 4. 明确未做 / 残余风险

| 项 | 说明 |
| --- | --- |
| **G1 实机目视** | 主机预览不等于真机（面板偏移/反色/字节序未验证）；**H 证据仍待设备** |
| idle 眨眼/小动作的**时间驱动** | 属 `pet_engine` 动画节拍，未做（契约 §8.3 已声明不做） |
| `>` 无动画 | 静态（契约 §8.3） |
| 标记位置/色为提案值 | `PET_MARK_OX/OY=56/-18`、`0xFFFF`/`0xFD20` 待批准 |
| mirror 对真实立绘无可见效果 | 立绘对称；mirror 供 M8 巡逻用 |

## 5. 主机预览

`./tools/sim.sh` → `sim/all-states.bmp`（6 态 1 图）+ `sim/0N-*.bmp`。
预览中 6 态已可辨：空闲 / **`>`** / **`!?`** / 笑 / 哭 / **灰+闭眼**。
