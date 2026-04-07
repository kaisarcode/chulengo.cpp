# chulengo - Multi-architecture Makefile
# Summary: Builds the raw chulengo binary and installer payloads.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

NAME = chulengo
INSTALLER_SRC = src/win/install.cpp
UNINSTALLER_SRC = src/win/uninstall.cpp
BIN_ROOT = bin
DEPS_ROOT = lib
TOOLCHAIN_ROOT = /usr/local/share/kaisarcode/toolchains
SRC = src/main.cpp src/pal.h

# Dynamic compiler resolution
CXX_x86_64_linux = g++
CXX_aarch64_linux = aarch64-linux-gnu-g++
NDK_VER = android-ndk-r27c
NDK_HOST = linux-x86_64
NDK_ROOT = $(TOOLCHAIN_ROOT)/ndk/$(NDK_VER)
NDK_BIN = $(NDK_ROOT)/toolchains/llvm/prebuilt/$(NDK_HOST)/bin
NDK_API = 24
CXX_aarch64_android = $(NDK_BIN)/aarch64-linux-android$(NDK_API)-clang++
CXX_x86_64_windows = x86_64-w64-mingw32-g++

# Compiler flags
# Vision support is enabled by default via CHULENGO_HAVE_MTMD
CXXFLAGS = -Wall -Wextra -Werror -O3 -std=c++17 \
    -DCHULENGO_HAVE_MTMD \
    -I$(DEPS_ROOT)/llama.cpp \
    -I$(DEPS_ROOT)/ggml \
    $(EXTRA_CXXFLAGS)
LDFLAGS_RUNTIME = -Wl,--no-as-needed $(EXTRA_LDFLAGS)
SYSLIBS_UNIX = -pthread
SYSLIBS_WIN = -lws2_32 -ladvapi32 -Wl,--no-insert-timestamp
WININSTALL = -lurlmon -lshell32 -ladvapi32 -lshlwapi -lcomctl32 \
    -Wl,--no-insert-timestamp

.PHONY: all clean build_arch x86_64/linux aarch64/linux aarch64/android x86_64/windows

all: x86_64/linux aarch64/linux aarch64/android x86_64/windows

x86_64/linux:
	$(MAKE) build_arch ARCH=x86_64 PLATFORM=linux CXX="$(CXX_x86_64_linux)" EXT=""

aarch64/linux:
	$(MAKE) build_arch ARCH=aarch64 PLATFORM=linux CXX="$(CXX_aarch64_linux)" EXT=""

aarch64/android:
	@if [ ! -f "$(CXX_aarch64_android)" ]; then \
		echo "[ERROR] NDK Compiler not found at: $(CXX_aarch64_android)"; \
		exit 1; \
	fi
	$(MAKE) build_arch ARCH=aarch64 PLATFORM=android CXX="$(CXX_aarch64_android)" EXT=""

x86_64/windows: install.exe uninstall.exe
	$(MAKE) build_arch ARCH=x86_64 PLATFORM=windows CXX="$(CXX_x86_64_windows)" EXT=".exe" \
	EXTRA_CXXFLAGS="-D_WIN32_WINNT=0x0601"

install.exe: $(INSTALLER_SRC)
	$(CXX_x86_64_windows) $(CXXFLAGS) -D_WIN32_WINNT=0x0601 -mwindows \
	$(INSTALLER_SRC) -o install.exe $(WININSTALL)

uninstall.exe: $(UNINSTALLER_SRC)
	$(CXX_x86_64_windows) $(CXXFLAGS) -D_WIN32_WINNT=0x0601 -mwindows \
	$(UNINSTALLER_SRC) -o uninstall.exe $(WININSTALL)

build_arch:
	mkdir -p $(BIN_ROOT)/$(ARCH)/$(PLATFORM)
	$(eval LLAMA_LIB = $(DEPS_ROOT)/llama.cpp/$(ARCH)/$(PLATFORM))
	$(eval GGML_LIB = $(DEPS_ROOT)/ggml/$(ARCH)/$(PLATFORM))
	$(eval SHARED_DEPS = $(GGML_LIB)/libggml.so $(GGML_LIB)/libggml-cpu.so \
	    $(GGML_LIB)/libggml-base.so $(LLAMA_LIB)/libllama.so $(LLAMA_LIB)/libmtmd.so)
	$(eval WIN_DEPS = $(GGML_LIB)/libggml.dll.a $(GGML_LIB)/libggml-cpu.dll.a \
	    $(GGML_LIB)/libggml-base.dll.a $(LLAMA_LIB)/libllama.dll.a $(LLAMA_LIB)/libmtmd.dll.a)
	$(eval DEPS = $(if $(filter windows,$(PLATFORM)),$(WIN_DEPS),$(SHARED_DEPS)))
	@for dep in $(DEPS); do \
		test -f "$$dep" || { echo "[ERROR] Missing $$dep"; exit 1; }; \
	done
	$(eval LOCAL_RPATH = -Wl,-rpath,'$$$$ORIGIN/../../$(LLAMA_LIB):$$$$ORIGIN/../../$(GGML_LIB)')
	$(eval INSTALL_RPATH = -Wl,-rpath,/usr/local/lib/kaisarcode/llama.cpp/$(ARCH)/$(PLATFORM):/usr/local/lib/kaisarcode/ggml/$(ARCH)/$(PLATFORM))
	$(eval RPATH_FLAGS = $(if $(filter windows,$(PLATFORM)),,$(LOCAL_RPATH) $(INSTALL_RPATH)))
	$(CXX) $(CXXFLAGS) src/main.cpp -o $(BIN_ROOT)/$(ARCH)/$(PLATFORM)/$(NAME)$(EXT) \
	$(LDFLAGS_RUNTIME) $(DEPS) \
	$(if $(filter windows,$(PLATFORM)),$(SYSLIBS_WIN),$(SYSLIBS_UNIX)) $(RPATH_FLAGS)

clean:
	rm -rf $(BIN_ROOT) install.exe uninstall.exe
