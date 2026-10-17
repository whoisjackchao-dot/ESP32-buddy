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

echo "== test_pet_voice =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_voice.c \
    firmware/components/pet/pet_voice.c \
    -o "$BUILD/test_pet_voice"
"$BUILD/test_pet_voice" || status=1

echo "== test_pet_link =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_link.c \
    firmware/components/pet/pet_link.c \
    -o "$BUILD/test_pet_link"
"$BUILD/test_pet_link" || status=1

echo "== test_pet_pomo =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_pomo.c \
    firmware/components/pet/pet_pomo.c \
    -o "$BUILD/test_pet_pomo"
"$BUILD/test_pet_pomo" || status=1

echo "== test_pet_linebuf =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_linebuf.c \
    firmware/components/pet/pet_linebuf.c \
    -o "$BUILD/test_pet_linebuf"
"$BUILD/test_pet_linebuf" || status=1

echo "== test_pet_button =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_button.c \
    firmware/components/pet/pet_button.c \
    -o "$BUILD/test_pet_button"
"$BUILD/test_pet_button" || status=1

echo "== test_pet_keymap =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_keymap.c \
    firmware/components/pet/pet_keymap.c \
    -o "$BUILD/test_pet_keymap"
"$BUILD/test_pet_keymap" || status=1

echo "== test_pet_menu =="
"$CC" -std=c11 -Wall -Wextra -Werror -g -I "$INC" \
    tests/test_pet_menu.c \
    firmware/components/pet/pet_menu.c \
    -o "$BUILD/test_pet_menu"
"$BUILD/test_pet_menu" || status=1

exit "$status"
