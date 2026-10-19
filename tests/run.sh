#!/usr/bin/env bash
# host 单测运行器（T 类）。用法：tests/run.sh
set -eu

CC="${CC:-cc}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

INC=firmware/components/pet/include
BUILD=tests/build
mkdir -p "$BUILD"

status=0

# Build one test binary; a compile failure is recorded but does not stop the
# remaining suites. -lm is required by pet_render (atan2/sin/lround).
build_and_run() {
    local name="$1"
    shift
    echo "== $name =="
    if ! "$CC" -std=c11 -Wall -Wextra -Werror -Wno-unknown-warning-option \
            -g -I "$INC" "$@" -o "$BUILD/$name" -lm; then
        status=1
        return 0
    fi
    "$BUILD/$name" || status=1
}

build_and_run test_pet_render \
    tests/test_pet_render.c \
    firmware/components/pet/pet_render.c \
    firmware/components/pet/pet_text.c

build_and_run test_pet_state \
    -D PET_DONE_DWELL_MS=5000 -D PET_DONE_EDGE_DEBOUNCE_MS=1500 -D PET_LINK_TIMEOUT_MS=15000 \
    tests/test_pet_state.c \
    firmware/components/pet/pet_state.c

build_and_run test_pet_card \
    tests/test_pet_card.c \
    firmware/components/pet/pet_render.c \
    firmware/components/pet/pet_text.c

build_and_run test_pet_voice \
    tests/test_pet_voice.c \
    firmware/components/pet/pet_voice.c

build_and_run test_pet_link \
    tests/test_pet_link.c \
    firmware/components/pet/pet_link.c

build_and_run test_pet_pomo \
    tests/test_pet_pomo.c \
    firmware/components/pet/pet_pomo.c

build_and_run test_pet_linebuf \
    tests/test_pet_linebuf.c \
    firmware/components/pet/pet_linebuf.c

build_and_run test_pet_button \
    tests/test_pet_button.c \
    firmware/components/pet/pet_button.c

build_and_run test_pet_keymap \
    tests/test_pet_keymap.c \
    firmware/components/pet/pet_keymap.c

build_and_run test_pet_menu \
    tests/test_pet_menu.c \
    firmware/components/pet/pet_menu.c

build_and_run test_pet_leisure \
    tests/test_pet_leisure.c \
    firmware/components/pet/pet_leisure.c

build_and_run test_pet_layout \
    tests/test_pet_layout.c \
    firmware/components/pet/pet_render.c \
    firmware/components/pet/pet_text.c

exit "$status"
