#!/bin/bash
# install.sh - Production installer for kc-app on Linux.
# Summary: Installs the current-architecture binary and required shared deps from master using wget.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

APP_ID="kc-app"
CORE_REPO_ROOT="https://raw.githubusercontent.com/kaisarcode/kc-bin"
BIN_DEP_REPO_ROOT="https://raw.githubusercontent.com/kaisarcode/kc-bin-dep"
SYS_BIN_DIR="/usr/local/bin"
DEPS=""

# Prints an error and exits.
# @param $1 Error message.
# @return Does not return.
fail() {
    printf "Error: %s\n" "$1" >&2
    exit 1
}

# Fails when one remote asset is unavailable.
# @param $1 Missing asset identifier.
# @return Does not return.
fail_unavailable() {
    fail "Remote asset is not available yet (repo may still be private): $1"
}

# Verifies that the installer is running on Linux.
# @return 0 on success.
require_linux() {
    [ "$(uname -s)" = "Linux" ] || fail "install.sh currently targets Linux only."
}

# Verifies that all required host tools are available.
# @return 0 on success.
require_tools() {
    command -v tar >/dev/null 2>&1 || fail "tar is required."
    command -v cp >/dev/null 2>&1 || fail "cp is required."
    command -v wget >/dev/null 2>&1 || fail "wget is required."
}

# Ensures the installer runs with root privileges.
# @param $@ Original script arguments.
# @return Does not return when re-executing.
ensure_root() {
    if [ "$(id -u)" -eq 0 ]; then
        return 0
    fi
    command -v sudo >/dev/null 2>&1 || fail "sudo is required."
    exec sudo bash "$0" "$@"
}

# Detects the current machine architecture name.
# @return Writes the resolved architecture to stdout.
detect_arch() {
    case "$(uname -m)" in
        x86_64) printf "x86_64" ;;
        aarch64|arm64) printf "aarch64" ;;
        armv8*|arm64-v8a) printf "arm64-v8a" ;;
        *) fail "Unsupported architecture: $(uname -m)" ;;
    esac
}

# Downloads one remote asset with wget.
# @param $1 Source URL.
# @param $2 Output path.
# @return 0 on success.
download_asset() {
    url="$1"
    out="$2"
    if ! wget -qO "$out" "$url"; then
        rm -f "$out"
        fail_unavailable "$url"
    fi
    if [ -s "$out" ] && [ "$(wc -c < "$out")" -lt 1024 ] && grep -q '^version https://git-lfs.github.com/spec/v1' "$out" 2>/dev/null; then
        media_url=$(echo "$url" | sed 's/raw.githubusercontent.com/media.githubusercontent.com\/media/')
        if ! wget -qO "$out" "$media_url"; then
            rm -f "$out"
            fail_unavailable "$media_url"
        fi
    fi
    [ -s "$out" ] || { rm -f "$out"; fail_unavailable "$url"; }
}

# Installs one dependency tree from kc-bin-dep.
# @param $1 Dependency identifier.
# @param $2 Architecture name.
# @param $3 Local mode flag (true/false).
# @return 0 on success.
install_dep() {
    dep="$1"
    arch="$2"
    branch="$3"
    wget -qO- "$BIN_DEP_REPO_ROOT/$branch/install.sh" | bash -s -- --branch "$branch" "$dep"
}

# Installs the current-architecture production binary.
# @param $1 Architecture name.
# @param $2 Local mode flag (true/false).
# @return 0 on success.
install_binary() {
    arch="$1"
    local_mode="$2"
    branch="$3"
    bin_rel="bin/${arch}/${APP_ID}"

    if [ "$local_mode" = "true" ]; then
        [ -f "./$bin_rel" ] || fail "Local binary not found at ./$bin_rel. Run 'make all' first."
        mkdir -p "$SYS_BIN_DIR"
        install -m 0755 "./$bin_rel" "$SYS_BIN_DIR/${APP_ID}"
        return 0
    fi

    tmp_dir="$(mktemp -d)"
    trap 'rm -rf "$tmp_dir"' RETURN

    download_asset "${CORE_REPO_ROOT}/$branch/kc-app/${bin_rel}" "$tmp_dir/${APP_ID}"
    mkdir -p "$SYS_BIN_DIR"
    install -m 0755 "$tmp_dir/${APP_ID}" "$SYS_BIN_DIR/${APP_ID}"

    rm -rf "$tmp_dir"
    trap - RETURN
}

# Runs the installer entry point.
# @param $@ Script arguments.
# @return 0 on success.
main() {
    require_linux
    require_tools
    ensure_root "$@"
    arch="$(detect_arch)"
    local local_mode="false"
    local branch="master"
    while [ $# -gt 0 ]; do
        case "$1" in
            --local) local_mode="true"; shift ;;
            --branch)
                [ $# -ge 2 ] || fail "Missing value for --branch"
                branch="$2"
                shift 2
                ;;
            --branch=*)
                branch="${1#--branch=}"
                shift
                ;;
            *) fail "Unknown argument: $1" ;;
        esac
    done

    for dep in $DEPS; do
        install_dep "$dep" "$arch" "$branch"
    done

    install_binary "$arch" "$local_mode" "$branch"
    printf "%s installed.\n" "${APP_ID}"
}

main "$@"
