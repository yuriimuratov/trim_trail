#!/usr/bin/env bash
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
  "$BIN" "$f" 0
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
  "$BIN" "$f" 10
  printf "a\nb\n" | cmp -s - "$f"
}

case_no_trailing_newline() {
  local f="$tmpdir/nonl.log"
  printf "x\ny" > "$f"  # no final newline
  "$BIN" "$f" 1
  printf "y" | cmp -s - "$f"
}

case_empty_file() {
  local f="$tmpdir/empty.log"
  : > "$f"
  "$BIN" "$f" 5
  [[ ! -s "$f" ]]
}

case_negative_behaves_like_zero() {
  local f="$tmpdir/neg.log"
  printf "data\n" > "$f"
  "$BIN" "$f" -5
  [[ ! -s "$f" ]]
}

case_handles_eintr_and_partial_writes() {
  build_fault_lib
  local f="$tmpdir/eintr.log"
  printf "l1\nl2\nl3\n" > "$f"
  LD_PRELOAD="$fault_lib" "$BIN" "$f" 2
  printf "l2\nl3\n" | cmp -s - "$f"
}

run_case "keep zero" case_keep_zero
run_case "keep 3 of 5" case_keep_three_of_five
run_case "keep > size" case_keep_more_than_exists
run_case "no trailing newline" case_no_trailing_newline
run_case "empty file" case_empty_file
run_case "negative treated as zero" case_negative_behaves_like_zero
run_case "EINTR/partial writes handled" case_handles_eintr_and_partial_writes

if (( fail > 0 )); then
  echo "Failed: $fail" >&2
  exit 1
fi

echo "All tests passed ($pass)."
