# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [KaisarCode Standards](https://kaisarcode.com).

## [1.0.1] - 2026-04-03

### Added
- Native CUDA support through dynamic backend discovery.
- Integration with modular `ggml` backends compiled with `GGML_BACKEND_DL=ON`.
- New architectural build targets: `aarch64`, `arm64-v8a` (Android NDK), and `win64` (MinGW).
- Multi-architecture `Makefile` supporting localized `lib/obj` and `lib/inc` vendoring.
- Automatic `SONAME` assignment to shared libraries via `patchelf` for standardized linking.

### Changed
- Refactored `Makefile` to use `-Wl,--no-as-needed`, forcing the inclusion of optional backends (like CUDA) into the binary's dependency table.
- Standardized binary `RPATH` to ensure portable execution from `bin/` directories.
- Full compliance with **KaisarCode Standards (KCS)** across all source files and scripts.

### Fixed
- Fixed KV state persistence logic in `chulengo_load_kv_state`: eliminated the destructive `llama_memory_seq_rm` call that was clearing the context upon loading a sessions's history.
- Resolved dynamic library loading issues in Linux by implementing relative path probing in `chulengo_load_backends`.
- Cleaned up compiler warnings treated as errors (`-Werror`) regarding logging suppression functions.
