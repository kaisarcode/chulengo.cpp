#!/bin/bash
# install.sh - Production installer for chulengo on Linux
# Summary: Installs the current-architecture binary and required runtime deps.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

APP_ID="chulengo"
REPO_ID="chulengo.cpp"
CORE_REPO_ROOT="https://raw.githubusercontent.com/kaisarcode/${REPO_ID}"
BIN_DEP_REPO_ROOT="https://raw.githubusercontent.com/kaisarcode/kc-bin-dep"
SYS_BIN_DIR="/usr/local/bin"
SYS_APP_DIR="/usr/local/lib/kaisarcode/apps"

# Prints one error and exits.
# @param $1 Error message.
# @return Does not return.
fail() {
    printf "Error: %s\n" "$1" >&2
    exit 1
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

# Reports one unavailable remote asset.
# @param $1 Missing asset identifier.
# @return Does not return.
fail_unavailable() {
    fail "Remote asset is not available yet (repo may still be private): $1"
}

# Downloads one remote asset.
# @param $1 Source URL.
# @param $2 Destination path.
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
    [ -s "$out" ] || {
        rm -f "$out"
        fail_unavailable "$url"
    }
}

# Detects the current machine architecture.
# @return Writes the resolved architecture to stdout.
detect_arch() {
    case "$(uname -m)" in
        x86_64) printf "x86_64" ;;
        aarch64|arm64) printf "aarch64" ;;
        armv8*|arm64-v8a) printf "arm64-v8a" ;;
        *) fail "Unsupported architecture: $(uname -m)" ;;
    esac
}

# Installs one runtime binary payload.
# @param $1 Source directory path.
# @param $2 Architecture name.
# @return 0 on success.
install_runtime_binary() {
    src_dir="$1"
    arch="$2"
    mkdir -p "$SYS_APP_DIR/$APP_ID/$arch"
    install -m 0755 "$src_dir/$APP_ID" "$SYS_APP_DIR/$APP_ID/$arch/$APP_ID"
}

# Installs one runtime wrapper in the global bin directory.
# @param $1 Architecture name.
# @return 0 on success.
install_runtime_wrapper() {
    arch="$1"
    wrapper_path="$SYS_BIN_DIR/$APP_ID"
    mkdir -p "$SYS_BIN_DIR"
    printf '%s\n' \
        '#!/bin/bash' \
        'set -e' \
        "exec \"$SYS_APP_DIR/$APP_ID/$arch/$APP_ID\" \"\$@\"" \
        | tee "$wrapper_path" >/dev/null
    chmod 0755 "$wrapper_path"
}

# Checks whether the installed payload already matches the target.
# @param $1 Source runtime binary path.
# @param $2 Architecture name.
# @return 0 when the payload is identical.
installed_matches_target() {
    src_bin="$1"
    arch="$2"
    [ -f "$SYS_BIN_DIR/$APP_ID" ] || return 1
    [ -f "$SYS_APP_DIR/$APP_ID/$arch/$APP_ID" ] || return 1
    cmp -s "$src_bin" "$SYS_APP_DIR/$APP_ID/$arch/$APP_ID"
}

# Runs the installer entry point.
# @param $@ Script arguments.
# @return 0 on success.
main() {
    [ "$(uname -s)" = "Linux" ] || fail "Only Linux is supported."
    ensure_root "$@"
    arch=$(detect_arch)
    local local_mode=false
    local branch="master"

    while [ $# -gt 0 ]; do
        case "$1" in
            --local) local_mode=true; shift ;;
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

    printf ">>> Installing dependencies for %s...\n" "$APP_ID"
    wget -qO- "$BIN_DEP_REPO_ROOT/$branch/install.sh" | bash -s -- --branch "$branch" llama.cpp

    printf ">>> Installing %s binary...\n" "$APP_ID"
    if [ "$local_mode" = true ]; then
        bin_path="./bin/$arch/$APP_ID"
        [ -f "$bin_path" ] || fail "Local binary not found at $bin_path. Run 'make all' first."
        if installed_matches_target "$bin_path" "$arch"; then
            printf "%s already installed and up to date. Skipping.\n" "$APP_ID"
            return 0
        fi
        install_runtime_binary "$(pwd)/bin/$arch" "$arch"
    else
        tmp_dir=$(mktemp -d)
        trap 'rm -rf "${tmp_dir:-}"' EXIT
        download_asset "$CORE_REPO_ROOT/$branch/bin/$arch/$APP_ID" "$tmp_dir/$APP_ID"
        if installed_matches_target "$tmp_dir/$APP_ID" "$arch"; then
            rm -rf "${tmp_dir:-}"
            trap - EXIT
            printf "%s already installed and up to date. Skipping.\n" "$APP_ID"
            return 0
        fi
        install_runtime_binary "$tmp_dir" "$arch"
        rm -rf "${tmp_dir:-}"
        trap - EXIT
    fi

    install_runtime_wrapper "$arch"
    printf "\033[1;32m[SUCCESS]\033[0m %s installed.\n" "$APP_ID"
}

main "$@"
