#!/bin/bash
# test.sh - Automated test suite for kc-app
# Summary: Tiered testing for ecosystem compliance and functional logic.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0
set -e

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
APP_ROOT="$SCRIPT_DIR"

# Prints failure details to standard output.
# @param message Error description.
# @return 1 on failure.
fail() {
    printf "\033[31m[FAIL]\033[0m %s\n" "$1"
    exit 1
}

# Prints success details to standard output.
# @param message Success description.
# @return 0 on success.
pass() {
    printf "\033[32m[PASS]\033[0m %s\n" "$1"
}

# Prepares environment and verifies local binary availability.
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
    export KC_BIN_EXEC="$APP_ROOT/bin/$ARCH/kc-app$EXT"

    if [ ! -f "$KC_BIN_EXEC" ]; then
        fail "Binary not found at $KC_BIN_EXEC."
    fi
    pass "Environment verified: using $KC_BIN_EXEC"
}

# Verifies help output and fail-fast CLI errors.
# @return 0 on success.
test_general() {
    if ! "$KC_BIN_EXEC" --help | grep -q "Options:"; then
        fail "General: Help flag failed."
    fi
    pass "General: Help flag verified."

    if "$KC_BIN_EXEC" --unknown >/dev/null 2>&1; then
        fail "General: Unknown flag should fail."
    fi
    pass "General: Unknown flag fail-fast verified."

    if "$KC_BIN_EXEC" --name >/dev/null 2>&1; then
        fail "General: Missing --name value should fail."
    fi
    pass "General: Missing value fail-fast verified."

    if "$KC_BIN_EXEC" --fd-in bad >/dev/null 2>&1; then
        fail "General: Invalid --fd-in should fail."
    fi
    pass "General: Invalid --fd-in fail-fast verified."

    if "$KC_BIN_EXEC" --fd-out bad >/dev/null 2>&1; then
        fail "General: Invalid --fd-out should fail."
    fi
    pass "General: Invalid --fd-out fail-fast verified."
}

# Verifies application-specific greeting logic.
# @return 0 on success.
test_functional() {
    OUTPUT=$(printf '\n' | "$KC_BIN_EXEC")
    if [ "$OUTPUT" != "Hello, World!" ]; then
        fail "Functional: Default greeting failed."
    fi
    pass "Functional: Default greeting verified."

    OUTPUT=$(printf '\n' | "$KC_BIN_EXEC" --name John)
    if [ "$OUTPUT" != "Hello, John!" ]; then
        fail "Functional: Greeting logic failed."
    fi
    pass "Functional: Greeting logic verified."

    OUTPUT=$(echo "John Doe" | "$KC_BIN_EXEC")
    if [ "$OUTPUT" != "Hello, John Doe!" ]; then
        fail "Functional: STDIN greeting failed."
    fi
    pass "Functional: STDIN greeting verified."

    OUTPUT=$(echo "Alice" | "$KC_BIN_EXEC" --name John)
    if [ "$OUTPUT" != "Hello, Alice!" ]; then
        fail "Functional: STDIN precedence failed."
    fi
    pass "Functional: STDIN precedence verified."

    exec 3<<'EOF'
Alice
EOF
    OUTPUT=$("$KC_BIN_EXEC" --fd-in 3)
    exec 3<&-
    if [ "$OUTPUT" != "Hello, Alice!" ]; then
        fail "Functional: Descriptor input greeting failed."
    fi
    pass "Functional: Descriptor input greeting verified."

    exec 3<<'EOF'

EOF
    OUTPUT=$("$KC_BIN_EXEC" --fd-in 3 --name Jane)
    exec 3<&-
    if [ "$OUTPUT" != "Hello, Jane!" ]; then
        fail "Functional: Descriptor input fallback to --name failed."
    fi
    pass "Functional: Descriptor input fallback to --name verified."

    exec 3<<'EOF'

EOF
    OUTPUT=$("$KC_BIN_EXEC" --fd-in 3)
    exec 3<&-
    if [ "$OUTPUT" != "Hello, World!" ]; then
        fail "Functional: Descriptor input fallback to default failed."
    fi
    pass "Functional: Descriptor input fallback to default verified."

    OUT_FILE=$(mktemp)
    trap 'rm -f "$OUT_FILE"' RETURN
    exec 4>"$OUT_FILE"
    printf '\n' | "$KC_BIN_EXEC" --name Jane --fd-out 4
    exec 4>&-
    OUTPUT=$(cat "$OUT_FILE")
    if [ "$OUTPUT" != "Hello, Jane!" ]; then
        fail "Functional: Descriptor output greeting failed."
    fi
    pass "Functional: Descriptor output greeting verified."
    rm -f "$OUT_FILE"
    trap - RETURN
}

# Entry point for the automated test suite.
# @return 0 on success.
run_tests() {
    test_setup
    test_general
    test_functional
    pass "All tests passed successfully."
}

run_tests
