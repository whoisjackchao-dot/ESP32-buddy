#!/usr/bin/env bash
# 构建并运行主机模拟器（把各状态渲染成 BMP 供目视）。
set -eu

CC="${CC:-cc}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

INC=firmware/components/pet/include
mkdir -p sim tests/build

"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    -D PET_DONE_DWELL_MS=5000 -D PET_DONE_EDGE_DEBOUNCE_MS=1500 -D PET_LINK_TIMEOUT_MS=15000 \
    tools/sim.c \
    firmware/components/pet/pet_render.c \
    firmware/components/pet/pet_text.c \
    firmware/components/pet/pet_link.c \
    firmware/components/pet/pet_state.c \
    -o tests/build/sim

./tests/build/sim firmware/components/pet/assets/robot.look
