---
description: 启动/查看 ESP32-buddy 入口守护（OpenCode 事件 → oc_events.log → COM5）
---
启动并检查 ESP32-buddy 入口守护。**必须使用绝对路径**（当前目录可能不是仓库）：

1. 运行：`bash tools/esp32-buddy-up.sh`
   （幂等：已在运行会输出 already running）
2. 运行：`bash /home/home/ESP32-buddy/tools/esp32-buddy-status.sh`
   查看 pid 存活、子进程状态 JSON（/tmp/esp32-buddy.state）与日志尾部（/tmp/esp32-buddy.log）。
3. 向用户报告：是否在运行、每个子进程 state/restarts、日志尾部是否有异常（反复 restart、
   backoff 递增、`check: FAIL` 或 MISSING）。

只做非破坏操作：**不要**运行 `esp32-buddy-down.sh`，**不要** kill 进程，**不要**改写日志/pidfile。
若反复重启或 --check 缺项，如实报告并等待用户指示，不要自行修复。
