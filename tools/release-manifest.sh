#!/usr/bin/env bash
# Generate flash/MANIFEST.json from an existing build (and optionally package).
#
# Usage:
#   tools/release-manifest.sh                 # requires a built BUILD_DIR
#   tools/release-manifest.sh --package       # run package-flash.sh first
#   BUILD_DIR=/path tools/release-manifest.sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${BUILD_DIR:-$HOME/esp-build/buddy}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"

if [ "${1:-}" = "--package" ]; then
    BUILD_DIR="$BUILD_DIR" "$ROOT/tools/package-flash.sh"
fi

if [ ! -f "$BUILD_DIR/flasher_args.json" ]; then
    echo "ERROR: missing $BUILD_DIR/flasher_args.json (build first)" >&2
    exit 2
fi

python3 - "$ROOT" "$BUILD_DIR" "$IDF_PATH" <<'PY'
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone

root, build_dir, idf_path = sys.argv[1:4]
args = json.load(open(os.path.join(build_dir, "flasher_args.json")))
files = args.get("flash_files", {})

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def idf_version():
    cmake = os.path.join(idf_path, "tools", "cmake", "version.cmake")
    if os.path.exists(cmake):
        vals = {}
        for line in open(cmake, encoding="utf-8"):
            line = line.strip()
            for key in ("MAJOR", "MINOR", "PATCH"):
                prefix = "set(IDF_VERSION_%s " % key
                if line.startswith(prefix):
                    vals[key] = line[len(prefix):].rstrip(") ")
        if len(vals) == 3:
            return "v%s.%s.%s" % (vals["MAJOR"], vals["MINOR"], vals["PATCH"])
    try:
        return subprocess.check_output(
            ["git", "-C", idf_path, "describe", "--tags", "--always"],
            text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unknown"

def git(*args):
    try:
        return subprocess.check_output(["git", "-C", root] + list(args),
                                       text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return ""

manifest = {
    "project": "ESP32-buddy",
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "commit": git("rev-parse", "HEAD"),
    "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
    "idf_version": idf_version(),
    "idf_path": idf_path,
    "chip": args.get("extra_esptool_args", {}).get("chip", "esp32s3"),
    "flash_mode": args.get("flash_settings", {}).get("flash_mode"),
    "flash_size": args.get("flash_settings", {}).get("flash_size"),
    "flash_freq": args.get("flash_settings", {}).get("flash_freq"),
    "files": [],
}

for offset, rel in sorted(files.items(), key=lambda kv: int(kv[0], 0)):
    path = os.path.join(build_dir, rel)
    if not os.path.exists(path):
        raise SystemExit("missing build artifact: %s" % path)
    manifest["files"].append({
        "path": rel,
        "offset": offset,
        "size": os.path.getsize(path),
        "sha256": sha256(path),
    })

out = os.path.join(root, "flash", "MANIFEST.json")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=2)
    f.write("\n")
print("wrote", out)
PY
