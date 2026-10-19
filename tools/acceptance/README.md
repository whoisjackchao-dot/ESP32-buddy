# M9 验收 runbook

> 用途：设备回来后按本清单逐项执行 G1–G7，并把观察记录写入 `evidence/M9/`。
> **纪律：没有真机观察记录，不得把任何 G 门槛标为 `PASS`；模拟器/BMP 只能作为预检。**

## 0. 准备

```bash
cd <repo>
bash tools/acceptance/run_host_checks.sh   # T/C 预检，生成 host 验收记录
```

设备侧：M5StickS3 接 USB-SLAVE 口，确认串口（Windows 常见 `COM5`）。
主机侧真实串口需要 `pyserial`（仅此一处第三方依赖）：

```bash
python3 -m pip install -r host/requirements.txt
```

主机侧单进程启动：

```bash
python3 host/pet_service.py --port COM5 --db ~/.local/share/opencode/opencode.db
```

可选 token：

```bash
PET_EVENT_TOKEN=s3cret python3 host/pet_service.py --port COM5
# 发事件时加 -H 'X-Pet-Token: s3cret'
```

发一个事件：

```bash
curl -sS -X POST http://127.0.0.1:8787/v1/events \
  -H 'Content-Type: application/json' \
  -d '{"version":1,"event":"task.start","title":"ACCEPTANCE","project":"buddy","elapsed_s":1}'
```

## G1 宠物 6 态

- 注入 `task.start` / `agent.input_required` / `task.done` / `task.error` / 停心跳 / `agent.idle`。
- 每态进入、退出各 10 次；记录正确次数。
- 判定：**20/20**（进入+退出各 10）目视可辨。
- 记录：截图/视频路径、失败序号、备注。

## G2 番茄钟

- `KEY2` 切番茄钟；`KEY1` 开始/暂停/继续；长按放弃。
- 完成/暂停/放弃各 10 次；重启设备后当日统计不丢。
- 判定：各 **10/10**；统计重启后一致 10/10。
- 记录：`done_count` / `focus_seconds` 前后值。

## G3 任务卡（含 3 卡栈）

- 单卡 20 次状态更新；记录从 POST 到上屏的时间。
- 3 卡：连续注入 4 个不同 title，确认只保留最新 3 张、同名更新置顶。
- 判定：20 次全部 **≤1s**；失败卡不剔除。
- 记录：时间戳/秒表、卡序、截断情况。

## G4 失联

- 停主机心跳 >15s；观察灰化/离线帧。
- 20 次试验，记录进入离线的时间。
- 判定：至少 **19/20** 在 15–17s 进入。
- 记录：每次起止时间。

## G5 稳定性

- 插电连续运行 **2 小时**；期间正常收发事件。
- 判定：无死机、无阻塞、无异常重启；记录 heap/uptime 日志。
- 记录：串口日志片段、重启次数。

## G6 本机事件入口

- 用任意脚本/`curl` POST 事件 20 次（含中文标题）。
- 判定：设备 **1s 内**显示，**20/20**。
- 记录：HTTP 状态码、设备上屏时间、失败样本。

## G7 语音

- 触发需要确认/完成/失败各 20 次。
- 判定：每次只播一次；不重复、不误播；工作中静音。
- 记录：听感、重复/漏播序号。

## 记录模板

复制 `evidence/M9/TEMPLATE.md`，填写：
- 日期、commit、设备、串口、操作者
- 每个 G 的原始观察/日志路径
- 结论只能是 `PASS` / `FAIL` / `BLOCKED` / `NOT_RUN`；**skip 不算 PASS**
