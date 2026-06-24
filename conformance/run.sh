#!/usr/bin/env bash
#
# Conformance / characterization harness (golden tests).
#
# Freezes the transpiler's output for a set of sample apps so refactors can be
# proven behavior-preserving:  transpile each app and diff against the recorded
# golden output.  Any difference fails the build.
#
#   conformance/run.sh record   # (re)generate golden outputs  -- use intentionally
#   conformance/run.sh verify   # diff current output vs golden -- use in CI
#
# Each app is transpiled in an isolated temp dir so stray files in the repo
# (.hexagen_modules, style.css, db_*.jsonl) cannot influence the output.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HF_CORE="$REPO_ROOT/hf_core"
APPS_DIR="$SCRIPT_DIR/apps"
GOLDEN_DIR="$SCRIPT_DIR/golden"

MODE="${1:-verify}"

if [ ! -x "$HF_CORE" ]; then
    echo "❌ hf_core not found/executable at $HF_CORE — run 'make hf_core' first."
    exit 2
fi
mkdir -p "$GOLDEN_DIR"

transpile_to_stdout() {
    # $1 = absolute path to .hx ; transpile in a clean temp dir for determinism.
    local hx="$1"
    local work
    work="$(mktemp -d)"
    cp "$hx" "$work/app.hx"
    ( cd "$work" && "$HF_CORE" transpile app.hx 2>/dev/null )
    local rc=$?
    rm -rf "$work"
    return $rc
}

fail=0
count=0
for hx in "$APPS_DIR"/*.hx; do
    name="$(basename "$hx" .hx)"
    golden="$GOLDEN_DIR/$name.cpp"
    count=$((count + 1))
    out="$(transpile_to_stdout "$hx")"
    if [ -z "$out" ]; then
        echo "❌ $name: transpile produced no output (crash?)"
        fail=$((fail + 1))
        continue
    fi
    if [ "$MODE" = "record" ]; then
        printf '%s\n' "$out" > "$golden"
        echo "📝 recorded $name ($(printf '%s\n' "$out" | wc -l) lines)"
    else
        if [ ! -f "$golden" ]; then
            echo "❌ $name: no golden file (run 'record' first)"
            fail=$((fail + 1))
            continue
        fi
        if diff -q <(printf '%s\n' "$out") "$golden" >/dev/null; then
            echo "✅ $name"
        else
            echo "❌ $name: output differs from golden"
            diff <(printf '%s\n' "$out") "$golden" | head -20
            fail=$((fail + 1))
        fi
    fi
done

echo "----"
if [ "$MODE" = "record" ]; then
    echo "Recorded $count golden file(s)."
    exit 0
fi
if [ "$fail" -eq 0 ]; then
    echo "✅ all $count conformance apps match their golden output"
    exit 0
else
    echo "❌ $fail/$count conformance apps differ"
    exit 1
fi
