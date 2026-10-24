---
description: 启动/查看 ESP32-buddy 入口守护
---
启动并检查 ESP32-buddy 入口守护（OpenCode 事件 → `oc_events.log` → COM5）：

1. 运行 `bash tools/esp32-buddy-up.sh`（幂等：已在运行则直接提示 already running）。
2. 运行 `bash tools/esp32-buddy-status.sh`，查看 pid 存活、子进程状态 JSON 与日志尾部。
3. 向用户报告结果：守护是否在运行、每个子进程的 state/restarts、日志最后几行是否有异常
   （反复 restart、backoff 递增、`check: FAIL` 或 MISSING 路径）。

只做非破坏操作：不要运行 `tools/esp32-buddy-down.sh`，不要 kill 任何进程，不要删除或改写
日志/pidfile。若发现子进程反复重启或 --check 缺项，如实报告并等待用户指示，不要自行修复。
