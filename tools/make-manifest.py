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
            # flash/*.bin 被 .gitignore 有意排除（不提交二进制）：
            # 若登记，新 clone 的 --check 会误报"文件消失"。
            # 产物哈希由 manifest.build 与 flash/MANIFEST.txt 负责。
            if name == "flash" and rel.endswith(".bin"):
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


def _read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read().strip()
    except OSError:
        return ""


def _idf_version(idf_path):
    """优先 version.txt；否则解析 esp_idf_version.h 的 MAJOR/MINOR/PATCH。"""
    if not idf_path:
        return ""
    txt = _read_text(os.path.join(idf_path, "version.txt"))
    if txt:
        return txt
    header = _read_text(os.path.join(
        idf_path, "components", "esp_common", "include", "esp_idf_version.h"))
    if not header:
        return ""
    parts = {}
    for line in header.splitlines():
        line = line.strip()
        if line.startswith("#define ESP_IDF_VERSION_"):
            fields = line.split()
            if len(fields) >= 3:
                parts[fields[1].rsplit("_", 1)[-1]] = fields[2]
    if all(k in parts for k in ("MAJOR", "MINOR", "PATCH")):
        return f"v{parts['MAJOR']}.{parts['MINOR']}.{parts['PATCH']}"
    return ""


def build_section(build_dir):
    """从 ESP-IDF 构建目录提取可复现构建信息（产物哈希 + 偏移 + 工具链）。

    build_dir 为空或缺少 flasher_args.json 时返回 None。
    """
    if not build_dir:
        return None
    build_dir = os.path.abspath(os.path.expanduser(build_dir))
    fa_path = os.path.join(build_dir, "flasher_args.json")
    if not os.path.isfile(fa_path):
        return None
    with open(fa_path, "r", encoding="utf-8") as f:
        fa = json.load(f)

    artifacts = []
    for offset, rel in sorted(fa.get("flash_files", {}).items(),
                              key=lambda kv: int(kv[0], 16)):
        path = os.path.join(build_dir, rel)
        if not os.path.isfile(path):
            continue
        artifacts.append({
            "file": rel.replace(os.sep, "/"),
            "offset": offset,
            "bytes": os.path.getsize(path),
            "sha256": _sha256(path),
        })

    pd_path = os.path.join(build_dir, "project_description.json")
    pd = {}
    if os.path.isfile(pd_path):
        with open(pd_path, "r", encoding="utf-8") as f:
            pd = json.load(f)
    idf_path = pd.get("idf_path", "")
    idf_ver = _idf_version(idf_path)

    extra = fa.get("extra_esptool_args", {})
    return {
        "build_dir": build_dir,
        "target": pd.get("target", extra.get("chip", "")),
        "chip": extra.get("chip", ""),
        "project_version": pd.get("project_version", ""),
        "idf_version": idf_ver,
        "flash_settings": fa.get("flash_settings", {}),
        "artifacts": artifacts,
    }


def build_manifest(build_dir=None):
    groups = collect()
    tracked = _git_field(["status", "--porcelain", "--untracked-files=no"])
    manifest = {
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
    build = build_section(build_dir)
    if build is not None:
        manifest["build"] = build
    return manifest


def check(manifest_path, build_dir=None):
    with open(manifest_path, "r", encoding="utf-8") as f:
        old = json.load(f)
    new = build_manifest(build_dir)
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

    # 构建产物：仅有构建目录时校验；没有则明确告警但不判失败（新 clone 无构建目录）
    old_build = old.get("build")
    if old_build:
        if new.get("build") is None:
            print("警告：manifest 登记了构建产物，但未提供可读的构建目录 → 跳过产物校验"
                  "（用 --build-dir 指定，如 ~/esp-build/buddy）", file=sys.stderr)
        else:
            old_art = {a["file"]: a for a in old_build.get("artifacts", [])}
            new_art = {a["file"]: a for a in new["build"].get("artifacts", [])}
            for name in sorted(set(old_art) | set(new_art)):
                a, b = old_art.get(name), new_art.get(name)
                if a is None:
                    problems.append(f"+ 产物未登记: {name}")
                elif b is None:
                    problems.append(f"- 产物缺失: {name}")
                elif a["sha256"] != b["sha256"]:
                    problems.append(f"~ 产物变化: {name}")

    if problems:
        print("manifest 不一致：", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print(f"manifest 一致（{new['totals']['files']} 文件 / {new['totals']['bytes']} 字节"
          + ("，含构建产物" if new.get("build") else "") + "）")
    return 0


def main():
    ap = argparse.ArgumentParser(description="生成/校验交付 manifest")
    ap.add_argument("--check", action="store_true", help="只校验，不写文件")
    ap.add_argument("--out", default=DEFAULT_OUT, help="输出路径")
    ap.add_argument("--build-dir", default=None,
                    help="ESP-IDF 构建目录（提供则登记 .bin 哈希与工具链），"
                         "如 ~/esp-build/buddy")
    args = ap.parse_args()

    if args.check:
        if not os.path.exists(args.out):
            print(f"缺少 {args.out}：先运行 python3 tools/make-manifest.py", file=sys.stderr)
            return 1
        return check(args.out, args.build_dir)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    manifest = build_manifest(args.build_dir)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2, sort_keys=False)
        f.write("\n")
    t = manifest["totals"]
    print(f"已写入 {args.out}：{t['files']} 文件 / {t['bytes']} 字节")
    b = manifest.get("build")
    if b:
        print(f"  构建产物 {len(b['artifacts'])} 个（{b['chip'] or b['target']}，"
              f"IDF {b['idf_version'] or '未知'}）")
    else:
        print("  未登记构建产物（未提供 --build-dir）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
