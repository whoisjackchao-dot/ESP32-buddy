# M1 · pet_render · T 类 RED→GREEN 记录

> 日期：2026-10-16　类别：**T**（业务逻辑，测试先行）
> 结论范围：**host 单测**通过，**不是**设备实机验收（H 类见 M1 剩余部分）。

## 证据字段

| 字段 | 内容 |
| --- | --- |
| 需求 ID | R20（分层）、R22（host 可测）；R01（宠物可见，最终） |
| 契约 ID | `contracts/M1-渲染分层契约冻结.md`（已冻结 2026-10-16） |
| 用例 ID | `LCD-01`（立绘格式）、`LCD-02`（surface 格式） |
| 待测组件 | `firmware/components/pet/pet_render.c`（`pet_look_parse` / `pet_render_pet`） |
| 输入 fixture | `firmware/components/pet/assets/robot.look`（自绘占位立绘，由 `tools/make-look.py` 生成）<br>**路径修订（2026-10-16）**：原记为 `tests/fixtures/robot.look`；因 IDF `EMBED_FILES` 不能指向组件目录之外，资产移入组件，**内容不变（sha256 一致）** |
| 代码版本 | `pet_render.c` sha256 `133a662b…df2974`；`test_pet_render.c`（已改为不依赖具体颜色）<br>**资产修订（2026-10-16，共三次）**：① 有身体 → ②「只有头」→ ③ **参考 vibebuddy README 实机截图重画「蓝色机器人头」**。`robot.look` sha256 依次 `9c7a0a5c…fd1939` → `be4bcb77…9cde4` → **`d5b39bd7…f84e2`**；生成器 `tools/make-look.py` sha256 **`a6e3db23…bf00c`**。因立绘会变，**渲染测试已重写为不依赖具体颜色**（期望值由测试独立解析资产得出），现为 **16 checks / 0 failed** |
| 环境 | WSL2 Ubuntu 26.04；`cc` = gcc 15.2.0-16ubuntu1 |
| 人工/实机证据 | **无**（本记录为 host 单测；实机见 M1 剩余部分） |

## 1. RED（真实失败，因缺少行为）

命令：`./tests/run.sh`（先以最小空壳 `pet_look_parse` 恒返回 NULL、`pet_render_pet` 空实现）

- 第一次 RED：退出码 **1**，断言失败
  - `FAIL tests/test_pet_render.c:98: l != NULL`（合法立绘包解析不出来）
  - `FAIL tests/test_pet_render.c:178: g_look != NULL`
- 第二次 RED（`pet_look_parse` 已实现、绘制仍空）：退出码 **1**
  - `FAIL tests/test_pet_render.c:108–111: get_px(&surface, 20*2, 15*2) == 0xFFFF`（2× 块未绘制）
  - `FAIL tests/test_pet_render.c:150: get_px(&surface, 20*2-8, 15*2-8) == 0xFFFF`（负原点未绘制）

> 失败在**业务断言**上，非"模块无法导入/工具缺失"，符合 RED 定义。

## 2. GREEN

命令：`./tests/run.sh` → `13 checks, 0 failed`，退出码 **0**；**重复运行第二次同样 PASS**（确定性）。

断言覆盖的行为：
- 非法 magic / 长度不符 → `pet_look_parse` 返回 NULL；
- 合法包 → 解析成功；
- 不透明像素按 **2×2** 块绘制，颜色为调色板值（RGB565 **little-endian**）；
- 透明像素（index 0）**不覆盖**底色；
- 右/下/负原点越界时**裁剪**，缓冲区末端 guard 字节未被写（无越界写）。

## 3. 残余风险（下一步输入）

| 风险 | 说明 |
| --- | --- |
| `mirror` / `gray` 未实现 | 属 M2（`pet_view_t` 已含该字段，`pet_render.c` 内已注明）；**当前传入将被忽略** |
| `pet_state` 无测试 | M1 只冻接口；状态机行为属 M2 |
| 未做变异测试 | 尚未验证"注入缺陷能否被测试抓住" |
| 仅 host 单测 | 实机（SPI/LVGL/面板偏移 52/40）**未验证** |
| fixture 是自绘占位 | 正式美术未定（`Q18` 已批准用占位） |

## 4. 归档

- 测试源码与 fixture 随仓库版本控制；无 VCS 提交时以本节哈希标识版本。
- **未**执行 `git commit` / `git push`（发布授权 `Q09` 未决）。
  > **补注（2026-10-16 后，不改原结论）**：`Q09` 其后**已批准并执行**——仓库已发布为公开仓库
  > `whoisjackchao-dot/ESP32-buddy`（见 `需求与决策基线` §4.1）。
  > 本条记录的是 **M1 会话当时**的状态，保留原日期与范围。
