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

echo "== test_pet_render =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_render.c \
    firmware/components/pet/pet_render.c \
    firmware/components/pet/pet_text.c \
    -o "$BUILD/test_pet_render"
"$BUILD/test_pet_render" || status=1

echo "== test_pet_state =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    -D PET_DONE_DWELL_MS=5000 -D PET_DONE_EDGE_DEBOUNCE_MS=1500 -D PET_LINK_TIMEOUT_MS=15000 \
    tests/test_pet_state.c \
    firmware/components/pet/pet_state.c \
    -o "$BUILD/test_pet_state"
"$BUILD/test_pet_state" || status=1

echo "== test_pet_card =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_card.c \
    firmware/components/pet/pet_render.c \
    firmware/components/pet/pet_text.c \
    -o "$BUILD/test_pet_card"
"$BUILD/test_pet_card" || status=1

echo "== test_pet_link =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_link.c \
    firmware/components/pet/pet_link.c \
    -o "$BUILD/test_pet_link"
"$BUILD/test_pet_link" || status=1

exit "$status"
