#!/usr/bin/env python3
"""生成/校验 M9 交付 manifest（路径 → sha256 → 字节数）。

用法：
    python3 tools/make-manifest.py            # 写 acceptance/manifest.json
    python3 tools/make-manifest.py --check    # 校验仓库是否与 manifest 一致
    python3 tools/make-manifest.py --out X    # 写到指定路径

设计要点（M9 契约：可复现构建 + manifest 含哈希）：
- 文件清单**按相对路径排序**，输出稳定 → 同一份源码两次生成只差 generated_at/git 字段。
- `--check` 忽略 generated_at 与 git 字段，只比对 path/sha256/bytes。
- 只登记**源码、资产、文档、第三方 vendored**；不登记 tests/build、__pycache__、.git。
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "acceptance", "manifest.json")

# 分组 → (根路径列表, 是否递归)
GROUPS = [
    ("firmware", ["firmware/components/pet", "firmware/main"]),
    ("host", ["host"]),
    ("tests", ["tests"]),
    ("docs", ["."]),          # 仅顶层 *.md + contracts/*.md + acceptance/*.md，见下
    ("flash", ["flash"]),
    ("third_party", ["firmware/components/es8311"]),
    # 交付证据也要可校验：M9 的"证据归档"是交付物之一
    ("evidence", ["evidence"]),
    # 许可与署名：M9 许可登记直接引用这些哈希
    ("legal", ["LICENSE", "NOTICE.md", ".opencode/skills/dayu/LICENSE"]),
]

SKIP_DIRS = {"__pycache__", ".git", "build", ".pytest_cache", "node_modules"}
SKIP_SUFFIXES = (".pyc", ".pyo", ".o", ".a")


def _rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _collect_docs():
    files = []
    for name in sorted(os.listdir(ROOT)):
        if name.endswith(".md") and os.path.isfile(os.path.join(ROOT, name)):
            files.append(os.path.join(ROOT, name))
    for sub in ("contracts", "acceptance"):
        base = os.path.join(ROOT, sub)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
            for fn in sorted(filenames):
                # manifest.json 自身不登记（每次生成都会变）
                if fn.endswith(".md"):
                    files.append(os.path.join(dirpath, fn))
    return files


def _walk(root):
    out = []
    if not os.path.exists(root):
        return out
    if os.path.isfile(root):
        return [root]
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for fn in sorted(filenames):
            if fn.endswith(SKIP_SUFFIXES):
                continue
            out.append(os.path.join(dirpath, fn))
    return out


def collect():
    groups = {}
    seen = set()
    for name, roots in GROUPS:
        entries = []
        if name == "docs":
            files = _collect_docs()
        else:
            files = []
            for r in roots:
                files.extend(_walk(os.path.join(ROOT, r)))
        for path in files:
            rel = _rel(path)
            if rel in seen:
                continue
            seen.add(rel)
            entries.append({
                "path": rel,
                "bytes": os.path.getsize(path),
                "sha256": _sha256(path),
            })
        entries.sort(key=lambda e: e["path"])
        groups[name] = entries
    return groups


def _git_field(args):
    try:
        out = subprocess.run(["git"] + args, cwd=ROOT, capture_output=True,
                             text=True, check=True)
        return out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def build_manifest():
    groups = collect()
    tracked = _git_field(["status", "--porcelain", "--untracked-files=no"])
    return {
        "schema": "esp32-buddy-manifest/1",
        "generated_at": datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds"),
        "git": {
            "commit": _git_field(["rev-parse", "HEAD"]),
            "branch": _git_field(["rev-parse", "--abbrev-ref", "HEAD"]),
            "tracked_clean": tracked == "",
        },
        "totals": {
            "files": sum(len(v) for v in groups.values()),
            "bytes": sum(e["bytes"] for v in groups.values() for e in v),
        },
        "groups": groups,
    }


def check(manifest_path):
    with open(manifest_path, "r", encoding="utf-8") as f:
        old = json.load(f)
    new = build_manifest()
    problems = []
    for group in sorted(set(old.get("groups", {})) | set(new["groups"])):
        old_map = {e["path"]: e for e in old.get("groups", {}).get(group, [])}
        new_map = {e["path"]: e for e in new["groups"].get(group, [])}
        for path in sorted(set(old_map) | set(new_map)):
            a, b = old_map.get(path), new_map.get(path)
            if a is None:
                problems.append(f"+ 新增未登记: {path}")
            elif b is None:
                problems.append(f"- 已登记但文件消失: {path}")
            elif a["sha256"] != b["sha256"]:
                problems.append(f"~ 内容变化: {path}")
    if problems:
        print("manifest 不一致：", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print(f"manifest 一致（{new['totals']['files']} 文件 / {new['totals']['bytes']} 字节）")
    return 0


def main():
    ap = argparse.ArgumentParser(description="生成/校验交付 manifest")
    ap.add_argument("--check", action="store_true", help="只校验，不写文件")
    ap.add_argument("--out", default=DEFAULT_OUT, help="输出路径")
    args = ap.parse_args()

    if args.check:
        if not os.path.exists(args.out):
            print(f"缺少 {args.out}：先运行 python3 tools/make-manifest.py", file=sys.stderr)
            return 1
        return check(args.out)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    manifest = build_manifest()
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2, sort_keys=False)
        f.write("\n")
    t = manifest["totals"]
    print(f"已写入 {args.out}：{t['files']} 文件 / {t['bytes']} 字节")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
