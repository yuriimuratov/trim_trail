#!/usr/bin/env bash
# trim_tail tests
# Copyright 2025 Yurii Muratov
# Licensed under the Apache License, Version 2.0 (see LICENSE)
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN="$ROOT/trim_tail"
CC_BIN="${CC:-cc}"

if [[ ! -x "$BIN" ]]; then
  echo "build the binary first (make)" >&2
  exit 1
fi

tmpdir=$(mktemp -d)
cleanup() { rm -rf "$tmpdir"; }
trap cleanup EXIT
fault_lib="$tmpdir/fault_inject.so"

build_fault_lib() {
  if [[ -f "$fault_lib" ]]; then
    return
  fi
  "$CC_BIN" -shared -fPIC -O2 -Wall -Wextra "$ROOT/tests/fault_inject.c" -o "$fault_lib" -ldl
}

pass=0
fail=0

run_case() {
  local name="$1"; shift
  if "$@"; then
    echo "[PASS] $name"
    pass=$((pass+1))
  else
    echo "[FAIL] $name" >&2
    fail=$((fail+1))
  fi
}

case_keep_zero() {
  local f="$tmpdir/zero.log"
  printf "one\ntwo\nthree\n" > "$f"
  "$BIN" "$f" --lines 0
  [[ ! -s "$f" ]]
}

case_keep_three_of_five() {
  local f="$tmpdir/five.log"
  printf "l1\nl2\nl3\nl4\nl5\n" > "$f"
  "$BIN" "$f" 3
  printf "l3\nl4\nl5\n" | cmp -s - "$f"
}

case_keep_more_than_exists() {
  local f="$tmpdir/short.log"
  printf "a\nb\n" > "$f"
  "$BIN" "$f" --lines 10
  printf "a\nb\n" | cmp -s - "$f"
}

case_no_trailing_newline() {
  local f="$tmpdir/nonl.log"
  printf "x\ny" > "$f"  # no final newline
  "$BIN" "$f" --lines 1
  printf "y" | cmp -s - "$f"
}

case_empty_file() {
  local f="$tmpdir/empty.log"
  : > "$f"
  "$BIN" "$f" --lines 5
  [[ ! -s "$f" ]]
}

case_negative_behaves_like_zero() {
  local f="$tmpdir/neg.log"
  printf "data\n" > "$f"
  "$BIN" "$f" --lines -5
  [[ ! -s "$f" ]]
}

case_handles_eintr_and_partial_writes() {
  build_fault_lib
  local f="$tmpdir/eintr.log"
  printf "l1\nl2\nl3\n" > "$f"
  LD_PRELOAD="$fault_lib" "$BIN" "$f" --lines 2
  printf "l2\nl3\n" | cmp -s - "$f"
}

case_bytes_aligns_to_line() {
  local f="$tmpdir/bytes.log"
  printf "a\nbc\ndef\n" > "$f"
  "$BIN" "$f" --bytes 2
  printf "def\n" | cmp -s - "$f"
}

case_bytes_suffixes() {
  local f="$tmpdir/bytes_suffix.log"
  printf "hello\n" > "$f"
  "$BIN" "$f" --bytes 1k
  printf "hello\n" | cmp -s - "$f"
}

case_bytes_noop_when_large() {
  local f="$tmpdir/bytes_noop.log"
  printf "12345\n" > "$f"
  "$BIN" "$f" --bytes 100
  printf "12345\n" | cmp -s - "$f"
}

case_bytes_suffix_k_real_file() {
  local f="$tmpdir/bytes_k_real.log"
  local f_src="$tmpdir/bytes_k_real_src.log"
  python3 - <<PY
import pathlib
out = pathlib.Path("$f")
with out.open("w", encoding="utf-8") as fh:
    for i in range(8000):  # ~64 KiB total, plenty for 1k trimming
        fh.write(f"line{i:05d}\\n")
PY
  cp "$f" "$f_src"
  "$BIN" "$f" --bytes 1k
  python3 - <<PY
import pathlib
src = pathlib.Path("$f_src")
data = src.read_bytes()
keep = 1000
target = max(0, len(data) - keep)
while target > 0 and data[target - 1] != 0x0A:
    target -= 1
expected = data[target:]
current = pathlib.Path("$f").read_bytes()
raise SystemExit(0 if expected == current else 1)
PY
}

case_bytes_suffix_m_real_file() {
  local f="$tmpdir/bytes_m_real.log"
  local f_src="$tmpdir/bytes_m_real_src.log"
  python3 - <<PY
import pathlib
out = pathlib.Path("$f")
with out.open("w", encoding="utf-8") as fh:
    for i in range(300000):  # ~3.6 MiB
        fh.write(f"row{i:06d}\\n")
PY
  cp "$f" "$f_src"
  "$BIN" "$f" --bytes 1m
  python3 - <<PY
import pathlib
src = pathlib.Path("$f_src")
data = src.read_bytes()
keep = 1_000_000
target = max(0, len(data) - keep)
while target > 0 and data[target - 1] != 0x0A:
    target -= 1
expected = data[target:]
current = pathlib.Path("$f").read_bytes()
raise SystemExit(0 if expected == current else 1)
PY
}

case_bytes_suffix_g_noop_real_file() {
  local f="$tmpdir/bytes_g_real.log"
  printf "sample\n" > "$f"
  "$BIN" "$f" --bytes 1g
  printf "sample\n" | cmp -s - "$f"
}

case_error_with_both_flags() {
  local f="$tmpdir/both_flags.log"
  printf "x\n" > "$f"
  if "$BIN" "$f" --lines 1 --bytes 1 >/dev/null 2>&1; then
    return 1
  fi
}

run_case "keep zero" case_keep_zero
run_case "keep 3 of 5" case_keep_three_of_five
run_case "keep > size" case_keep_more_than_exists
run_case "no trailing newline" case_no_trailing_newline
run_case "empty file" case_empty_file
run_case "negative treated as zero" case_negative_behaves_like_zero
run_case "EINTR/partial writes handled" case_handles_eintr_and_partial_writes
run_case "bytes align to line" case_bytes_aligns_to_line
run_case "bytes suffixes (k/m/g)" case_bytes_suffixes
run_case "bytes no-op when >= size" case_bytes_noop_when_large
run_case "bytes suffix k on real file" case_bytes_suffix_k_real_file
run_case "bytes suffix m on real file" case_bytes_suffix_m_real_file
run_case "bytes suffix g no-op on real file" case_bytes_suffix_g_noop_real_file
run_case "error with both flags" case_error_with_both_flags

if (( fail > 0 )); then
  echo "Failed: $fail" >&2
  exit 1
fi

echo "All tests passed ($pass)."
