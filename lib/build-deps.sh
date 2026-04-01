#!/bin/bash
# build-deps.sh - Shared dependency builder for chulengo
# Summary: Builds the shared llama.cpp, mtmd, and ggml runtime files used by chulengo.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)
SRC_ROOT_DEFAULT="/home/kaisar/Work/kc-bin-dep/src/llama.cpp"
SRC_ROOT="${LLAMA_CPP_SRC:-$SRC_ROOT_DEFAULT}"
BUILD_ROOT="${CHULENGO_BUILD_ROOT:-/tmp/chulengo-shared-build}"
NDK_TOOLCHAIN="/usr/local/share/kaisarcode/toolchains/ndk/android-ndk-r27c/build/cmake/android.toolchain.cmake"

# Prints one failure and exits.
# @param $1 Failure message.
# @return Does not return.
fail() {
    printf "Error: %s\n" "$1" >&2
    exit 1
}

# Verifies the upstream source tree exists.
# @return 0 on success.
require_source_tree() {
    [ -d "$SRC_ROOT" ] || fail "llama.cpp source tree not found at $SRC_ROOT"
    [ -f "$SRC_ROOT/CMakeLists.txt" ] || fail "CMakeLists.txt not found at $SRC_ROOT"
}

# Returns whether one command exists.
# @param $1 Command name.
# @return 0 when the command exists.
has_command() {
    command -v "$1" >/dev/null 2>&1
}

# Creates one clean destination folder for copied runtime files.
# @param $1 Stack name.
# @param $2 Architecture name.
# @return 0 on success.
prepare_dest_dir() {
    stack="$1"
    arch="$2"
    rm -rf "$ROOT_DIR/lib/obj/$stack/$arch"
    mkdir -p "$ROOT_DIR/lib/obj/$stack/$arch"
}

# Finds one regular file by glob inside one directory tree.
# @param $1 Directory path.
# @param $2 File glob.
# @return Writes the matched path to stdout.
find_regular_file() {
    dir_path="$1"
    file_glob="$2"
    find "$dir_path" -type f -name "$file_glob" | sort | head -n 1
}

# Copies one ELF shared library under one stable public name.
# @param $1 Source file path.
# @param $2 Stack name.
# @param $3 Architecture name.
# @param $4 Public file name.
# @return 0 on success.
copy_elf_shared() {
    src="$1"
    stack="$2"
    arch="$3"
    public_name="$4"
    dest="$ROOT_DIR/lib/obj/$stack/$arch/$public_name"
    [ -f "$src" ] || fail "Missing shared library: $src"
    install -m 0644 "$src" "$dest"
    patchelf --set-soname "$public_name" "$dest"
}

# Rewrites one internal ELF dependency to its public flat name when present.
# @param $1 File path.
# @param $2 Current dependency name.
# @param $3 Public dependency name.
# @return 0 on success.
replace_needed_if_present() {
    file_path="$1"
    current_name="$2"
    public_name="$3"
    if readelf -d "$file_path" | grep -F "[$current_name]" >/dev/null 2>&1; then
        patchelf --replace-needed "$current_name" "$public_name" "$file_path"
    fi
}

# Normalizes internal shared-library dependencies to flat public names.
# @param $1 File path.
# @return 0 on success.
normalize_elf_dependencies() {
    file_path="$1"
    replace_needed_if_present "$file_path" "libggml-base.so.0" "libggml-base.so"
    replace_needed_if_present "$file_path" "libggml-cpu.so.0" "libggml-cpu.so"
    replace_needed_if_present "$file_path" "libggml-cuda.so.0" "libggml-cuda.so"
    replace_needed_if_present "$file_path" "libggml.so.0" "libggml.so"
    replace_needed_if_present "$file_path" "libllama.so.0" "libllama.so"
    replace_needed_if_present "$file_path" "libmtmd.so.0" "libmtmd.so"
}

# Copies one shared library without ELF patching.
# @param $1 Source file path.
# @param $2 Stack name.
# @param $3 Architecture name.
# @param $4 Public file name.
# @return 0 on success.
copy_runtime_file() {
    src="$1"
    stack="$2"
    arch="$3"
    public_name="$4"
    [ -f "$src" ] || fail "Missing runtime file: $src"
    install -m 0644 "$src" "$ROOT_DIR/lib/obj/$stack/$arch/$public_name"
}

# Configures one shared build tree.
# @param $1 Build directory path.
# @param $2 Extra CMake arguments.
# @return 0 on success.
configure_build() {
    build_dir="$1"
    shift
    rm -rf "$build_dir"
    cmake -S "$SRC_ROOT" -B "$build_dir" \
        -DBUILD_SHARED_LIBS=ON \
        -DGGML_STATIC=OFF \
        -DGGML_BACKEND_DL=OFF \
        -DLLAMA_BUILD_COMMON=ON \
        -DLLAMA_BUILD_TESTS=OFF \
        -DLLAMA_BUILD_EXAMPLES=OFF \
        -DLLAMA_BUILD_SERVER=OFF \
        -DLLAMA_BUILD_TOOLS=ON \
        "$@"
}

# Builds one minimal shared target set.
# @param $1 Build directory path.
# @param $2 Extra target names.
# @return 0 on success.
build_targets() {
    build_dir="$1"
    shift
    cmake --build "$build_dir" --target llama mtmd ggml ggml-base ggml-cpu "$@" -j"$(nproc)"
}

# Detects one CUDA architecture list for local x86_64 builds.
# @return Writes the CUDA architecture list to stdout.
detect_cuda_architectures() {
    if has_command nvidia-smi; then
        arch_value=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader,nounits 2>/dev/null | head -n 1 | tr -d '.')
        if [ -n "$arch_value" ]; then
            printf "%s" "$arch_value"
            return 0
        fi
    fi
    printf "61;70;75;80;86;89"
}

# Copies one Linux or Android runtime set.
# @param $1 Build directory path.
# @param $2 Architecture name.
# @return 0 on success.
copy_unix_runtime_set() {
    build_dir="$1"
    arch="$2"
    ggml_base_src=$(find_regular_file "$build_dir" 'libggml-base.so*')
    ggml_cpu_src=$(find_regular_file "$build_dir" 'libggml-cpu.so*')
    ggml_src=$(find_regular_file "$build_dir" 'libggml.so*')
    llama_src=$(find_regular_file "$build_dir" 'libllama.so*')
    mtmd_src=$(find_regular_file "$build_dir" 'libmtmd.so*')
    prepare_dest_dir "ggml" "$arch"
    prepare_dest_dir "llama.cpp" "$arch"
    copy_elf_shared "$ggml_base_src" "ggml" "$arch" "libggml-base.so"
    copy_elf_shared "$ggml_cpu_src" "ggml" "$arch" "libggml-cpu.so"
    copy_elf_shared "$ggml_src" "ggml" "$arch" "libggml.so"
    copy_elf_shared "$llama_src" "llama.cpp" "$arch" "libllama.so"
    copy_elf_shared "$mtmd_src" "llama.cpp" "$arch" "libmtmd.so"
    normalize_elf_dependencies "$ROOT_DIR/lib/obj/ggml/$arch/libggml-base.so"
    normalize_elf_dependencies "$ROOT_DIR/lib/obj/ggml/$arch/libggml-cpu.so"
    normalize_elf_dependencies "$ROOT_DIR/lib/obj/ggml/$arch/libggml.so"
    normalize_elf_dependencies "$ROOT_DIR/lib/obj/llama.cpp/$arch/libllama.so"
    normalize_elf_dependencies "$ROOT_DIR/lib/obj/llama.cpp/$arch/libmtmd.so"
}

# Builds and stores one Linux x86_64 runtime set.
# @return 0 on success.
build_x86_64() {
    build_dir="$BUILD_ROOT/x86_64"
    if has_command nvcc; then
        cuda_archs=$(detect_cuda_architectures)
        configure_build "$build_dir" \
            -DGGML_CUDA=ON \
            -DCMAKE_CUDA_ARCHITECTURES="$cuda_archs"
        build_targets "$build_dir" ggml-cuda
    else
        configure_build "$build_dir"
        build_targets "$build_dir"
    fi
    copy_unix_runtime_set "$build_dir" "x86_64"
    if has_command nvcc; then
        ggml_cuda_src=$(find_regular_file "$build_dir" 'libggml-cuda.so*')
        copy_elf_shared "$ggml_cuda_src" "ggml" "x86_64" "libggml-cuda.so"
        normalize_elf_dependencies "$ROOT_DIR/lib/obj/ggml/x86_64/libggml-cuda.so"
    fi
}

# Builds and stores one Linux aarch64 runtime set.
# @return 0 on success.
build_aarch64() {
    build_dir="$BUILD_ROOT/aarch64"
    configure_build "$build_dir" \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc \
        -DCMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++
    build_targets "$build_dir"
    copy_unix_runtime_set "$build_dir" "aarch64"
}

# Builds and stores one Android arm64-v8a runtime set.
# @return 0 on success.
build_arm64_v8a() {
    build_dir="$BUILD_ROOT/arm64-v8a"
    configure_build "$build_dir" \
        -DCMAKE_TOOLCHAIN_FILE="$NDK_TOOLCHAIN" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM=android-24
    build_targets "$build_dir"
    copy_unix_runtime_set "$build_dir" "arm64-v8a"
}

# Builds and stores one Windows win64 runtime set.
# @return 0 on success.
build_win64() {
    build_dir="$BUILD_ROOT/win64"
    configure_build "$build_dir" \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=/usr/bin/x86_64-w64-mingw32-gcc \
        -DCMAKE_CXX_COMPILER=/usr/bin/x86_64-w64-mingw32-g++ \
        -DCMAKE_C_FLAGS="-D_WIN32_WINNT=0x0601 -DWINVER=0x0601" \
        -DCMAKE_CXX_FLAGS="-D_WIN32_WINNT=0x0601 -DWINVER=0x0601"
    build_targets "$build_dir"
    prepare_dest_dir "ggml" "win64"
    prepare_dest_dir "llama.cpp" "win64"
    copy_runtime_file "$(find_regular_file "$build_dir" 'ggml-base.dll')" "ggml" "win64" "ggml-base.dll"
    copy_runtime_file "$(find_regular_file "$build_dir" 'libggml-base.dll.a')" "ggml" "win64" "libggml-base.dll.a"
    copy_runtime_file "$(find_regular_file "$build_dir" 'ggml-cpu.dll')" "ggml" "win64" "ggml-cpu.dll"
    copy_runtime_file "$(find_regular_file "$build_dir" 'libggml-cpu.dll.a')" "ggml" "win64" "libggml-cpu.dll.a"
    copy_runtime_file "$(find_regular_file "$build_dir" 'ggml.dll')" "ggml" "win64" "ggml.dll"
    copy_runtime_file "$(find_regular_file "$build_dir" 'libggml.dll.a')" "ggml" "win64" "libggml.dll.a"
    copy_runtime_file "$(find_regular_file "$build_dir" 'libllama.dll')" "llama.cpp" "win64" "libllama.dll"
    copy_runtime_file "$(find_regular_file "$build_dir" 'libllama.dll.a')" "llama.cpp" "win64" "libllama.dll.a"
    copy_runtime_file "$(find_regular_file "$build_dir" 'libmtmd.dll')" "llama.cpp" "win64" "libmtmd.dll"
    copy_runtime_file "$(find_regular_file "$build_dir" 'libmtmd.dll.a')" "llama.cpp" "win64" "libmtmd.dll.a"
}

# Runs the full dependency refresh.
# @return 0 on success.
main() {
    require_source_tree
    build_x86_64
    build_aarch64
    build_arm64_v8a
    build_win64
}

main "$@"
