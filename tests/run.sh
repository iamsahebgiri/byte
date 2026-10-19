#!/usr/bin/env bash
#
# Runs every tests/*.byte program and verifies its output against the
# snapshot in tests/golden/<name>.snap.
#
# Each snapshot file records the full observable behaviour of one test:
#
#   exit <code>              0 ok / 65 compile error / 70 runtime error
#   -- stdout --             program output follows immediately after
#   -- stderr --             compiler/runtime errors follow
#
# Workflow:
#   ./tests/run.sh               # verify all tests
#   ./tests/run.sh <name> ...    # verify only some tests (no .byte suffix needed)
#   ./tests/run.sh --update      # regenerate snapshots after an intentional change
#
# Exit status: 0 = all pass, 1 = some fail, 2 = setup error.

set -u

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bin="$root/../build/byte"
golden="$root/golden"
timeout=5
update=0
selected=()

usage() {
  sed -n '3,16p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -u|--update) update=1 ;;
    --bin) bin="$2"; shift ;;
    --timeout) timeout="$2"; shift ;;
    -h|--help) usage ;;
    *) selected+=("$1") ;;
  esac
  shift
done

if [[ ! -f "$bin" ]]; then
  echo "runner: binary not found: $bin" >&2
  echo "        build first: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && make -C build" >&2
  exit 2
fi

mkdir -p "$golden"
passed=0
failed=0
updated=0
failures=()

# 'perl alarm' acts as a watchdog so an infinite loop in the VM cannot hang the
# suite. A SIGALRM-raised process reports exit code 142.

# Assembles the observable behaviour of one run into $1
build_snapshot() {
  local out="$1"
  printf '%s\n' "exit $status" >"$out"
  printf '%s\n' "-- stdout --" >>"$out"
  cat "$actual_out" >>"$out"
  printf '%s\n' "-- stderr --" >>"$out"
  cat "$actual_err" >>"$out"
}

for file in "$root"/*.byte; do
  name="$(basename "${file%.byte}")"

  keep=1
  if [[ ${#selected[@]} -gt 0 ]]; then
    keep=0
    for sel in "${selected[@]}"; do
      [[ "$name" == "$sel" ]] && keep=1
    done
  fi
  [[ $keep -eq 1 ]] || continue

  actual_out="$(mktemp)"
  actual_err="$(mktemp)"
  actual_snap="$golden/$name.snap.actual"

  perl -e 'alarm shift; exec @ARGV' "$timeout" "$bin" "$file" \
    >"$actual_out" 2>"$actual_err"
  status=$?

  if [[ $status -eq 142 ]]; then
    failed=$((failed + 1))
    failures+=("$name: exceeded the ${timeout}s timeout (infinite loop?)")
    rm -f "$actual_out" "$actual_err" "$actual_snap"
    continue
  fi

  build_snapshot "$actual_snap"

  expected_snap="$golden/$name.snap"

  if [[ $update -eq 1 ]]; then
    mv "$actual_snap" "$expected_snap"
    echo "UPDATED  $name (exit $status)"
    updated=$((updated + 1))
    rm -f "$actual_out" "$actual_err"
    continue
  fi

  if [[ ! -f $expected_snap ]]; then
    rm -f "$actual_out" "$actual_err" "$actual_snap"
    failed=$((failed + 1))
    failures+=("$name: no snapshot yet (run ./tests/run.sh --update $name)")
    continue
  fi

  if cmp -s "$actual_snap" "$expected_snap"; then
    passed=$((passed + 1))
    echo "PASS     $name"
  else
    failed=$((failed + 1))
    failures+=("$name: output differs")
    echo "FAIL     $name  (output differs)"
    diff -u "$expected_snap" "$actual_snap" | head -15
  fi

  rm -f "$actual_out" "$actual_err" "$actual_snap"
done

echo
if [[ ${#failures[@]} -gt 0 ]]; then
  echo "result: $passed passed, $failed failed, $updated updated"
  printf 'failure: %s\n' "${failures[@]}"
  exit 1
fi
echo "result: $passed passed, 0 failed, $updated updated"
exit 0
