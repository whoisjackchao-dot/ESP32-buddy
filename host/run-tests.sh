#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
exec python3 -m unittest discover -s host/tests -p 'test_*.py' -v
