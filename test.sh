#!/bin/bash
# test.sh - Automated test suite for chulengo
# Summary: Validates the compact embed and infer public surface.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
APP_ROOT="$SCRIPT_DIR"
MODEL_ROOT="${CHULENGO_MODEL_ROOT:-./models}"

# Prints one failure and exits.
# @param $1 Failure message.
# @return Does not return.
fail() {
    printf "\033[31m[FAIL]\033[0m %s\n" "$1"
    exit 1
}

# Prints one success line.
# @param $1 Success message.
# @return 0 on success.
pass() {
    printf "\033[32m[PASS]\033[0m %s\n" "$1"
}

# Counts EOT bytes in one file.
# @param $1 File path.
# @return 0 on success.
count_eot() {
    od -An -t u1 "$1" | tr ' ' '\n' | grep -c '^4$' || true
}

# Resolves the current build artifact.
# @return 0 on success.
test_setup() {
    ARCH=$(uname -m)
    [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "aarch64" ] || ARCH="arm64-v8a"
    case "$ARCH" in
        x86_64) EXT="" ;;
        aarch64) EXT="" ;;
        arm64-v8a) EXT="" ;;
        *) EXT="" ;;
    esac
    export CHULENGO_BIN="$APP_ROOT/bin/$ARCH/chulengo$EXT"
    [ -x "$CHULENGO_BIN" ] || fail "Binary not found at $CHULENGO_BIN."
    if [ "$(uname -s)" = "Linux" ]; then
        export LD_LIBRARY_PATH="$APP_ROOT/lib/obj/llama.cpp/$ARCH:$APP_ROOT/lib/obj/ggml/$ARCH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        if ldd "$CHULENGO_BIN" | grep -q 'not found'; then
            ldd "$CHULENGO_BIN"
            fail "Shared runtime dependencies are missing."
        fi
    fi
    pass "Environment verified: using $CHULENGO_BIN"
}

# Verifies command help and fail-fast parsing.
# @return 0 on success.
test_general() {
    HELP_OUT=$("$CHULENGO_BIN" --help)
    printf '%s\n' "$HELP_OUT" | grep -q 'chulengo embed' || fail "Help output missing embed command."
    printf '%s\n' "$HELP_OUT" | grep -q 'chulengo infer' || fail "Help output missing infer command."
    printf '%s\n' "$HELP_OUT" | grep -q -- '--type' || fail "Help output missing --type."
    printf '%s\n' "$HELP_OUT" | grep -q -- '--predict' || fail "Help output missing infer flags."
    pass "General: Help output verified."

    if "$CHULENGO_BIN" >/dev/null 2>&1; then fail "Missing command should fail."; fi
    if "$CHULENGO_BIN" unknown >/dev/null 2>&1; then fail "Unknown command should fail."; fi
    if "$CHULENGO_BIN" embed >/dev/null 2>&1; then fail "Missing --model should fail."; fi
    if "$CHULENGO_BIN" embed --model >/dev/null 2>&1; then fail "Missing --model value should fail."; fi
    if "$CHULENGO_BIN" embed --model /tmp/x --type video >/dev/null 2>&1; then fail "Unsupported --type should fail."; fi
    if "$CHULENGO_BIN" infer --model /tmp/x --type image <<< 'x' >/dev/null 2>&1; then fail "infer --type image should fail in the first cut."; fi
    if "$CHULENGO_BIN" infer --model /tmp/x --top-p 2 <<< 'x' >/dev/null 2>&1; then fail "Out-of-range --top-p should fail."; fi
    if "$CHULENGO_BIN" infer --model /tmp/x --lora-scale 1.0 <<< 'x' >/dev/null 2>&1; then fail "--lora-scale without --lora should fail."; fi
    pass "General: Fail-fast CLI verified."
}

# Verifies gateway errors without relying on model success.
# @return 0 on success.
test_gateway() {
    if printf 'hello' | "$CHULENGO_BIN" embed --model /tmp/not-real.gguf >/dev/null 2>&1; then
        fail "Text embedding should fail with a missing model."
    fi
    if printf 'hello' | "$CHULENGO_BIN" infer --model /tmp/not-real.gguf >/dev/null 2>&1; then
        fail "Inference should fail with a missing model."
    fi
    if "$CHULENGO_BIN" embed --type image --model "$MODEL_ROOT/emb/jina-embeddings-v4-vllm-retrieval.Q4_K_M.gguf" <"$MODEL_ROOT/img/1.png" >/dev/null 2>&1; then
        fail "Image embedding should require --mmproj."
    fi
    pass "Functional: Gateway validation verified."
}

# Verifies real text embedding output.
# @return 0 on success.
test_real_embed_text() {
    OUT_FILE=$(mktemp)
    trap 'rm -f "$OUT_FILE"' RETURN
    printf 'vector search' | "$CHULENGO_BIN" embed --model "$MODEL_ROOT/emb/bge-small.gguf" >"$OUT_FILE"
    grep -q '^\[' "$OUT_FILE" || fail "Text embedding did not emit JSON."
    [ "$(count_eot "$OUT_FILE")" -eq 1 ] || fail "Text embedding did not emit exactly one EOT."
    rm -f "$OUT_FILE"
    trap - RETURN
    pass "Functional: Real text embedding verified."
}

# Verifies real text inference output.
# @return 0 on success.
test_real_infer() {
    OUT_FILE=$(mktemp)
    trap 'rm -f "$OUT_FILE"' RETURN
    printf 'Say hello in five words.' | "$CHULENGO_BIN" infer --model "$MODEL_ROOT/llm/SmolV2/SmolLM2-135M-Instruct-Q4_K_M.gguf" --predict 16 --gpu 0 >"$OUT_FILE"
    [ -s "$OUT_FILE" ] || fail "Inference produced no output."
    [ "$(count_eot "$OUT_FILE")" -eq 1 ] || fail "Inference did not emit exactly one EOT."
    rm -f "$OUT_FILE"
    trap - RETURN
    pass "Functional: Real inference verified."
}

# Verifies real image embedding output when available.
# @return 0 on success.
test_real_embed_image() {
    if [ "$(uname -m)" != "x86_64" ]; then
        pass "Functional: Image embedding skipped on non-x86_64 host."
        return 0
    fi
    OUT_FILE=$(mktemp)
    trap 'rm -f "$OUT_FILE"' RETURN
    "$CHULENGO_BIN" embed \
        --type image \
        --model "$MODEL_ROOT/emb/jina-embeddings-v4-vllm-retrieval.Q4_K_M.gguf" \
        --mmproj "$MODEL_ROOT/emb/jina-embeddings-v4-vllm-retrieval.mmproj-Q8_0.gguf" \
        <"$MODEL_ROOT/img/1.png" \
        >"$OUT_FILE"
    grep -q '^\[' "$OUT_FILE" || fail "Image embedding did not emit JSON."
    [ "$(count_eot "$OUT_FILE")" -eq 1 ] || fail "Image embedding did not emit exactly one EOT."
    rm -f "$OUT_FILE"
    trap - RETURN
    pass "Functional: Real image embedding verified."
}

# Runs the full test suite.
# @return 0 on success.
run_tests() {
    test_setup
    test_general
    test_gateway
    test_real_embed_text
    test_real_infer
    test_real_embed_image
    pass "All tests passed successfully for chulengo."
}

run_tests
