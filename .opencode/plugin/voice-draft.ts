/**
 * esp32-buddy voice draft plugin (MIT-adapted from
 * github.com/whoisjackchao-dot/esp32-s3-m5, plugin/src/index.ts + bridge/server.ts
 * + plugin/safeAppend.ts + plugin/slotPrompt.ts + plugin/capture.ts).
 *
 * What it does: listens on 127.0.0.1:$BUDDY_VOICE_PORT (default 8790) and
 * appends {"text":"…"} bodies to the current session prompt draft via
 * POST /v1/draft/append. It NEVER submits: no ref.submit anywhere, the user
 * still presses Enter. The host bridge (host/file_to_serial.py) detects the
 * device's voice.text console line and POSTs here (best-effort).
 *
 * 兼容性警告：本机 OpenCode 1.19.4 的 session_prompt 槽为"追加"语义，加载后会出现
 * 两个输入框 → 已停用（未注册）；语音文本改走主机剪贴板（file_to_serial 默认）。
 * 以下说明保留给 1.18.x 或后续修复参考。
 *
 * Loading (per reference spike/FINDINGS.md, OpenCode 1.18.31/32 evidence):
 * TUI plugins are NOT loaded from opencode.json `plugin` (that key is for
 * server plugins and rejects TUI modules with "must default export an object
 * with server()"). This file is registered in .opencode/tui.json:
 *   {"plugin": ["<absolute path to this file>"]}
 * Takes effect on the next opencode start / new session. Changing this file
 * or the port requires restarting opencode.
 *
 * Deviations from the reference (documented in
 * evidence/2026-10-25-语音写入OpenCode-T.md): no bearer token / descriptor
 * file / epoch / operation ledger — one fixed loopback port, schema-check +
 * loopback bind + Origin rejection kept; body is {"text"} only.
 */

// Minimal local shapes for the TUI API pieces we touch; the reference narrows
// upstream type defects at runtime (its capture.ts), we avoid the dependency
// entirely and validate at runtime instead.
interface VoicePromptRef {
    current: { input: string; parts: unknown[] } & Record<string, unknown>;
    set: (next: Record<string, unknown>) => void;
}

import { appendFileSync } from "node:fs";

const DBG_LOG = "/tmp/voice-draft.log"; /* 诊断：slot 渲染/append 路径 */
function dbg(msg: string): void {
    try {
        appendFileSync(DBG_LOG, `${new Date().toISOString()} ${msg}\n`);
    } catch {
        /* best-effort */
    }
}

const PLUGIN_ID = "esp32-buddy-voice-draft";
const PLUGIN_VERSION = "0.1.0";
const BODY_MAX_BYTES = 64 * 1024;

function voicePort(): number {
    const raw = process.env.BUDDY_VOICE_PORT;
    const n = raw ? Number(raw) : NaN;
    return Number.isInteger(n) && n > 0 && n < 65536 ? n : 8790;
}

/** Reference core/types.ts appendWithSeparator: newline-join when the draft
 * does not already end in whitespace, else direct concat. */
function appendWithSeparator(existing: string, addition: string): string {
    if (existing.length > 0 && !/\s$/.test(existing)) {
        return `${existing}\n${addition}`;
    }
    return `${existing}${addition}`;
}

function json(res: import("node:http").ServerResponse, status: number, body: unknown): void {
    res.writeHead(status, { "content-type": "application/json" });
    res.end(JSON.stringify(body));
}

/** Reference safeAppend: read -> append -> set -> readback, one sync block;
 * any failure yields "uncertain", no retry, no submit. */
function appendToRef(ref: VoicePromptRef, text: string): "applied" | "uncertain" {
    try {
        const current = ref.current;
        const nextInput = appendWithSeparator(current.input, text);
        ref.set({ ...current, input: nextInput });
        const readback = ref.current;
        const applied =
            readback.input === nextInput && readback.parts.length === current.parts.length;
        return applied ? "applied" : "uncertain";
    } catch {
        return "uncertain";
    }
}

export default {
    id: PLUGIN_ID,
    // TUI plugin module shape ({id, tui}); a server-plugin default export
    // would be a function returning hooks - do not mix the two.
    tui: async (api: any) => {
        const port = voicePort();

        // Reference capture.ts RefStore: live Prompt ref per session.
        const refs = new Map<string, VoicePromptRef>();

        // session_prompt is a REPLACE slot (reference FINDINGS.md): the
        // renderer is invoked as (context, data) and `data` carries the host
        // props. Every host prop MUST be forwarded into api.ui.Prompt and the
        // host ref callback MUST be called - dropping props regresses the
        // input box into a "new session" box (reference §5 incident).
        api.slots.register({
            id: `${PLUGIN_ID}-slots`,
            slots: {
                session_prompt: (context: unknown, data: any) => {
                    void context;
                    const props = (data ?? {}) as Record<string, any>;
                    dbg(`render session=${props.session_id ?? "-"} visible=${props.visible} disabled=${props.disabled}`);
                    return api.ui.Prompt({
                        sessionID: props.session_id,
                        visible: props.visible,
                        disabled: props.disabled,
                        onSubmit: props.on_submit,
                        ref: (ref: VoicePromptRef | undefined) => {
                            props.ref?.(ref);
                            if (props.session_id) {
                                if (ref) refs.set(props.session_id, ref);
                                else refs.delete(props.session_id);
                            }
                        },
                    });
                },
            },
        });

        // Reference index.ts currentSessionID: sessionID only on the "session"
        // route; runtime check because the route union cannot be narrowed.
        function currentSessionID(): string | undefined {
            const route = api.route.current;
            if (route?.name !== "session") return undefined;
            const id = route.params?.["sessionID"];
            return typeof id === "string" ? id : undefined;
        }

        function handleAppend(
            req: import("node:http").IncomingMessage,
            res: import("node:http").ServerResponse,
            raw: Buffer,
        ): void {
            if (raw.length > BODY_MAX_BYTES) {
                json(res, 400, { error: { code: "payload_too_large" } });
                return;
            }
            let parsed: unknown;
            try {
                parsed = JSON.parse(raw.toString("utf-8"));
            } catch {
                json(res, 400, { error: { code: "invalid_request" } });
                return;
            }
            const text =
                typeof parsed === "object" && parsed !== null
                    ? (parsed as Record<string, unknown>)["text"]
                    : undefined;
            if (typeof text !== "string" || text.length === 0) {
                json(res, 400, { error: { code: "invalid_request" } });
                return;
            }
            // Reference index.ts binding(): refuse cross-session writes.
            const sessionID = currentSessionID();
            const ref = sessionID ? refs.get(sessionID) : undefined;
            dbg(`append route=${sessionID ?? "-"} refs=[${[...refs.keys()].join(",")}] hasRef=${!!ref}`);
            if (!ref) {
                // Not in a session (or prompt unmounted): nothing to fill.
                json(res, 200, { status: "uncertain", reason: "no_prompt_ref" });
                return;
            }
            const before = ref.current?.input?.length ?? -1;
            const result = appendToRef(ref, text);
            dbg(`append result=${result} inputBefore=${before} inputAfter=${ref.current?.input?.length ?? -1}`);
            json(res, 200, { status: result });
        }

        // node:http so the file also runs under a plain Node/Bun runtime;
        // bind loopback only - that is the whole auth boundary (deviation:
        // reference additionally uses a bearer token + 0600 descriptor).
        const server = (await import("node:http")).createServer(
            (req: import("node:http").IncomingMessage, res: import("node:http").ServerResponse) => {
                try {
                    // Reference server.ts §2: Origin present -> 403 (browser
                    // CSRF/DNS-rebinding defense), Host must be loopback.
                    if (req.headers["origin"] !== undefined) {
                        json(res, 403, { error: { code: "origin_forbidden" } });
                        return;
                    }
                    const host = String(req.headers["host"] ?? "");
                    if (!/^127\.0\.0\.1(:\d+)?$/.test(host) && !/^localhost(:\d+)?$/.test(host)) {
                        json(res, 403, { error: { code: "host_forbidden" } });
                        return;
                    }
                    const path = (req.url ?? "").split("?")[0];
                    if (path === "/v1/debug") {
                        json(res, 200, {
                            plugin: PLUGIN_ID,
                            route: currentSessionID() ?? null,
                            routeName: api.route?.current?.name ?? null,
                            refs: [...refs.keys()],
                        });
                        return;
                    }
                    if (path === "/v1/health") {
                        if (req.method !== "GET") {
                            json(res, 405, { error: { code: "method_not_allowed" } });
                            return;
                        }
                        json(res, 200, { alive: true, plugin: PLUGIN_ID, pluginVersion: PLUGIN_VERSION });
                        return;
                    }
                    if (path === "/v1/draft/append") {
                        if (req.method !== "POST") {
                            json(res, 405, { error: { code: "method_not_allowed" } });
                            return;
                        }
                        const media = String(req.headers["content-type"] ?? "")
                            .split(";")[0]?.trim().toLowerCase();
                        if (media !== "application/json") {
                            json(res, 415, { error: { code: "unsupported_media_type" } });
                            return;
                        }
                        const chunks: Buffer[] = [];
                        req.on("data", (c: Buffer) => chunks.push(c));
                        req.on("end", () => handleAppend(req, res, Buffer.concat(chunks)));
                        req.on("error", () => json(res, 400, { error: { code: "invalid_request" } }));
                        return;
                    }
                    json(res, 404, { error: { code: "not_found" } });
                } catch {
                    // 无伤插入: never bubble into opencode's event loop.
                    try {
                        json(res, 500, { error: { code: "internal" } });
                    } catch {
                        /* socket already gone */
                    }
                }
            },
        );

        server.on("error", () => {
            // Port taken (second instance) or privileged: keep opencode alive;
            // voice.text then degrades to console log only.
            console.error(`[${PLUGIN_ID}] listener unavailable on 127.0.0.1:${port}`);
        });
        server.listen(port, "127.0.0.1");

        api.lifecycle.onDispose(() => {
            server.close();
            refs.clear();
        });
    },
};
