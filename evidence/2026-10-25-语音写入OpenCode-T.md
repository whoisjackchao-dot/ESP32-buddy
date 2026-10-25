# 语音写入 OpenCode（T）

> 日期：2026-10-25　·　范围：host 侧（Python 桥 + OpenCode 插件），设备语音转写 → OpenCode 提示词草稿追加
> 依据：T 任务「Add the host-side path that takes the device's voice transcript
> (`{"v":1,"cmd":"voice.text","text":"…"}` on the USB serial console) and appends it
> to the OpenCode prompt, following github.com/whoisjackchao-dot/esp32-s3-m5 (MIT)」

## 1. 参考项目契约（实测原文，clone 至 /tmp/opencode/m5voice）

| 项 | 参考实现（esp32-s3-m5） |
| --- | --- |
| 插件文件 | `plugin/src/index.ts`（入口，id `sticks3-voice-draft`）、`plugin/src/bridge/server.ts`（Bun HTTP 桥）、`plugin/src/plugin/safeAppend.ts`（追加语义）、`plugin/src/plugin/slotPrompt.ts`（session_prompt 槽位宿主）、`plugin/src/plugin/capture.ts`（RefStore/registerSlotPlugin） |
| 端点契约 | `POST /v1/draft/append`，`Content-Type: application/json`，body `{protocolVersion, tuiEpoch, sessionID, operationID, transactionGeneration, text}`；另 `GET /v1/health`、`GET /v1/binding`、`GET|POST /v1/operations/:id[/cancel]`、`GET /v1/state/stream`(SSE) |
| 认证 | `Authorization: Bearer <token>`（随机 32 字节 hex）；另有 Origin 头 → 403、Host 头必须 127.0.0.1/localhost → 403；仅绑定 127.0.0.1 |
| 端口/发现 | `Bun.serve` 127.0.0.1 随机端口；插件把 `{url, token, tuiEpoch, pluginVersion}` 写 `~/.local/share/sticks3-opencode/bridge.json`（0600），host 经 `STICKS3_BRIDGE_URL/TOKEN/TUI_EPOCH/SESSION_ID` 环境变量读取（`host/src/voice_append.cpp`，10s 接收上限，仅允许 loopback URL） |
| 注册方式 | **TUI 插件**：`.opencode/tui.json` `{"plugin": ["<绝对路径.ts>"]}`（spike/FINDINGS.md 实证：`opencode.json` 的 `plugin` 键是服务器插件，放 TUI 模块报 "must default export an object with server()"） |
| 追加语义 | `session_prompt` 为 replace 槽位，renderer 双参 `(context, data)`，宿主 props（`session_id/on_submit/visible/disabled/ref`）在第 2 参，必须全部转发进 `api.ui.Prompt`（漏转发的故障记录见 FINDINGS §5）；经 ref 回调捕获 `ref`，追加 = 读 `ref.current` → `appendWithSeparator`（非空白结尾则 `\n` 连接）→ `ref.set({...current, input})` → 回读校验；**绝不调用 ref.submit** |

## 2. 适配（本仓库）

- **插件** `.opencode/plugin/voice-draft.ts`（单文件，无外部依赖，node:http，Bun/Node 均可加载）：
  - 端口固定 `127.0.0.1:$BUDDY_VOICE_PORT`（默认 **8790**，取代参考的随机端口+描述文件发现）。
  - 契约简化为宿主桥所需子集：`POST /v1/draft/append` body `{"text": "…"}`（schema 校验 400、media 415、method 405、未知路径 404）；保留参考的 Origin→403 与 Host-loopback→403 检查。
  - **去掉 Bearer token / 0600 描述文件 / epoch / operation 台账**：loopback 绑定即边界（单机 dev 工具场景；偏差已在 §4 声明）。
  - 追加语义原样保留：session_prompt 槽位双参 renderer 全量转发宿主 props、RefStore 按 session 捕获 ref、`appendWithSeparator` + 回读校验、任何异常返回 `uncertain`、**无 ref.submit**。
- **装载** `.opencode/tui.json`：`{"plugin": ["/home/home/ESP32-buddy/.opencode/plugin/voice-draft.ts"]}`（参考证据要求绝对路径；换机器需改此路径）。生效方式：**重启 opencode / 新会话**。文件按任务要求放在 `.opencode/plugin/` 下，但装载登记在 tui.json（理由见 §1 注册方式行）。
- **宿主桥** `host/file_to_serial.py`：console reader 新增 `extract_voice_text`（JSON 解析，容错一切非法行）、`post_voice_text`（best-effort POST，1s 超时 `VOICE_POST_TIMEOUT`，吞异常，urllib 显式绕过代理）；CLI 新增 `--voice-url`（默认 `http://127.0.0.1:8790/v1/draft/append`）与 `--no-voice`；启动横幅追加 `voice=<url|off>`。原有 IP 发现路径不变。

## 3. TDD：RED → GREEN

- **RED**：先写 `tools/tests/test_voice_append.py`（19 用例：提取 7、POST 3、reader 5、CLI 4）。运行
  `python3 -m unittest tools.tests.test_voice_append`：
  `Ran 19 tests / FAILED (failures=1, errors=18)`，代表性错误：
  `AttributeError: module 'file_to_serial' has no attribute 'post_voice_text'`、
  `AssertionError: 'voice_url' not found in ... console_reader 参数`（功能缺失，预期失败原因）。
- **GREEN**：实现后同命令 → **Ran 19 tests / OK**。中途修正 1 处**测试自身** bug：
  两个 `main` 用例的 fake_sleep 只喂了控制台行、未追加日志字节，导致串口从未打开、reader 未启动（对齐
  test_dual_transport 既有写法后通过）。
- **回归**：`python3 -m unittest discover -s tools/tests` → **Ran 140 tests / OK**（121 基线 + 19 新增）；
  `./host/run-tests.sh` → **Ran 77 tests / OK**（既有 host 测试不受影响）。
- **插件烟雾验证**（bun，mock TUI api + 真实 listener）：health 200 alive；`hello`、`打开绿灯`（UTF-8）、
  连续追加均 `{"status":"applied"}` 且换行拼接正确；坏 JSON/坏 schema 400、非 JSON media 415、GET 405、
  Origin 头 403、未知路径 404。`bun build` 解析通过，默认导出 `{id, tui}`。

## 4. 未验证（诚实边界）

- **真实 OpenCode 插件装载**：本机无 OpenCode 实例；`.opencode/tui.json` + `.opencode/plugin/` 双路径组合
  未在真实 opencode 启动中验证（参考证据只证明 tui.json 路径；服务器插件自动发现器对 TUI 模块文件的反应未知）。
- **真实追加**：session_prompt 槽位/ref 行为按参考 1.18.31/32 二进制级证据实现，未对真实 TUI 回归
  （props 转发语义随 opencode 版本漂移的风险由参考 FINDINGS §5 记录）。
- **设备侧 voice.text**：本任务 host-only；固件发出 `{"v":1,"cmd":"voice.text"}` 行的格式/时序未在真机验证。
- 8790 端口被占时插件仅打日志降级（语音功能静默失效），无自动重试。

## 5. 使用

```bash
# 1) opencode 侧：tui.json 已登记插件，重启 opencode（或新开会话）即生效
#    换机器：编辑 .opencode/tui.json 为本仓库 voice-draft.ts 的绝对路径
# 2) 桥侧：默认即开
python3 host/file_to_serial.py --log <log> [--voice-url http://127.0.0.1:8790/v1/draft/append] [--no-voice]
# 设备说出的话 → 控制台 {"v":1,"cmd":"voice.text","text":"…"} → 追加进 opencode 输入框（不发送，需手动回车）
```
