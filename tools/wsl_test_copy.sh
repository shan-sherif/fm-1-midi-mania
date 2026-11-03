#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 fm-1-midi-mania contributors
# The full test suite in a disposable copy on WSL's own Linux disk: much faster than from the Windows folder
# (/mnt/c), where every file the suite opens or writes crosses the Windows <-> Linux bridge.
#
#   tools/wsl_test_copy.sh            sync, build (gen, sdl), the judge test, then tests/run_tests.sh
#   tools/wsl_test_copy.sh --sync     sync only
#
# Run it in WSL from the repository (the Windows folder). The copy, TEST_COPY (default ~/fm-1-midi-mania-test), is a
# mirror: never edit it, every run overwrites it. Its build/ is kept between runs; Felucca/ and .git/ are not copied.
# The log: TEST_COPY/build/run_tests.log
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
DST=${TEST_COPY:-$HOME/fm-1-midi-mania-test}
mkdir -p "$DST"
rsync -a --delete --exclude /build/ --exclude /Felucca/ --exclude /.git/ "$SRC/" "$DST/"
echo "synced $SRC -> $DST"
[ "$1" = --sync ] && exit 0
cd "$DST"
t0=$(date +%s)
./build.sh gen
./build.sh sdl
mkdir -p build
cc -std=gnu99 -Wall -Wextra tests/judge_test.c -o build/judge_test
build/judge_test
t1=$(date +%s)
rc=0
sh tests/run_tests.sh > build/run_tests.log 2>&1 || rc=$?
t2=$(date +%s)
echo "build + judge test: $((t1 - t0)) s, run_tests.sh: $((t2 - t1)) s, exit $rc (log: $DST/build/run_tests.log)"
exit $rc
