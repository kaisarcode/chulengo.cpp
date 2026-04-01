#!/bin/bash
# install.sh - Production installer for chulengo on Linux
# Summary: Installs the current-architecture binary and shared runtime dependencies.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

APP_ID="chulengo"
REPO_ID="chulengo.cpp"
CORE_REPO_ROOT="https://raw.githubusercontent.com/kaisarcode/${REPO_ID}/master"
SYS_BIN_DIR="/usr/local/bin"
SYS_APP_DIR="/usr/local/lib/kaisarcode/apps"
SYS_DEP_DIR="/usr/local/lib/kaisarcode"

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

# Installs one runtime dependency payload.
# @param $1 Source directory path.
# @param $2 Stack name.
# @param $3 Architecture name.
# @return 0 on success.
install_runtime_deps() {
    src_dir="$1"
    stack="$2"
    arch="$3"
    mkdir -p "$SYS_DEP_DIR/obj/$stack/$arch"
    find "$src_dir/$stack/$arch" -maxdepth 1 -type f | while IFS= read -r dep_path; do
        install -m 0644 "$dep_path" "$SYS_DEP_DIR/obj/$stack/$arch/$(basename "$dep_path")"
    done
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
        "export LD_LIBRARY_PATH=\"$SYS_DEP_DIR/obj/llama.cpp/$arch:$SYS_DEP_DIR/obj/ggml/$arch\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}\"" \
        "exec \"$SYS_APP_DIR/$APP_ID/$arch/$APP_ID\" \"\$@\"" \
        | tee "$wrapper_path" >/dev/null
    chmod 0755 "$wrapper_path"
}

# Runs the installer entry point.
# @param $@ Script arguments.
# @return 0 on success.
main() {
    [ "$(uname -s)" = "Linux" ] || fail "Only Linux is supported."
    ensure_root "$@"
    arch=$(detect_arch)
    local local_mode=false

    while [ $# -gt 0 ]; do
        case "$1" in
            --local) local_mode=true; shift ;;
            *) fail "Unknown argument: $1" ;;
        esac
    done

    printf ">>> Installing %s runtime dependencies...\n" "$APP_ID"
    if [ "$local_mode" = true ]; then
        bin_path="./bin/$arch/$APP_ID"
        [ -f "$bin_path" ] || fail "Local binary not found at $bin_path. Run 'make all' first."
        [ -d "./lib/obj/llama.cpp/$arch" ] || fail "Local llama.cpp dependencies not found for $arch. Run './lib/build-deps.sh' first."
        [ -d "./lib/obj/ggml/$arch" ] || fail "Local ggml dependencies not found for $arch. Run './lib/build-deps.sh' first."
        install_runtime_deps "$(pwd)/lib/obj" "llama.cpp" "$arch"
        install_runtime_deps "$(pwd)/lib/obj" "ggml" "$arch"
        printf ">>> Installing %s binary...\n" "$APP_ID"
        install_runtime_binary "$(pwd)/bin/$arch" "$arch"
    else
        mkdir -p "$SYS_DEP_DIR/obj/llama.cpp/$arch"
        mkdir -p "$SYS_DEP_DIR/obj/ggml/$arch"
        mkdir -p "$SYS_APP_DIR/$APP_ID/$arch"
        download_asset "$CORE_REPO_ROOT/bin/$arch/$APP_ID" "$SYS_APP_DIR/$APP_ID/$arch/$APP_ID"
        chmod 0755 "$SYS_APP_DIR/$APP_ID/$arch/$APP_ID"
        download_asset "$CORE_REPO_ROOT/lib/obj/llama.cpp/$arch/libllama.so" "$SYS_DEP_DIR/obj/llama.cpp/$arch/libllama.so"
        download_asset "$CORE_REPO_ROOT/lib/obj/llama.cpp/$arch/libmtmd.so" "$SYS_DEP_DIR/obj/llama.cpp/$arch/libmtmd.so"
        download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/libggml.so" "$SYS_DEP_DIR/obj/ggml/$arch/libggml.so"
        download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/libggml-base.so" "$SYS_DEP_DIR/obj/ggml/$arch/libggml-base.so"
        download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/libggml-cpu.so" "$SYS_DEP_DIR/obj/ggml/$arch/libggml-cpu.so"
        if [ "$arch" = "x86_64" ]; then
            download_asset "$CORE_REPO_ROOT/lib/obj/ggml/$arch/libggml-cuda.so" "$SYS_DEP_DIR/obj/ggml/$arch/libggml-cuda.so"
        fi
    fi

    install_runtime_wrapper "$arch"
    printf "\033[1;32m[SUCCESS]\033[0m %s installed.\n" "$APP_ID"
}

main "$@"
