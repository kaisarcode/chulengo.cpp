# kc-app - Multi-architecture Makefile
# Summary: Build system with per-app artifacts and global toolchains.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

NAME       = kc-app
SRC        = src/main.c
INSTALLER_SRC = src/win/install.c
BIN_ROOT   = bin
TOOLCHAIN_ROOT = /usr/local/share/kaisarcode/toolchains

NDK_VER     = android-ndk-r27c
NDK_HOST    = linux-x86_64
NDK_ROOT    = $(TOOLCHAIN_ROOT)/ndk/$(NDK_VER)
NDK_BIN     = $(NDK_ROOT)/toolchains/llvm/prebuilt/$(NDK_HOST)/bin

CC_x86_64    = gcc
CC_aarch64   = aarch64-linux-gnu-gcc
NDK_API      = 24
CC_arm64_v8a = $(NDK_BIN)/aarch64-linux-android$(NDK_API)-clang
CC_win64     = x86_64-w64-mingw32-gcc

CFLAGS  = -Wall -Wextra -Werror -O3 -std=c11
WINSOCK = -lws2_32 -ladvapi32 -Wl,--no-insert-timestamp
WININSTALL = -lurlmon -lshell32 -ladvapi32 -lshlwapi -lcomctl32 -Wl,--no-insert-timestamp

.PHONY: all clean build_arch x86_64 aarch64 arm64-v8a win64

all: x86_64 aarch64 arm64-v8a win64

x86_64: $(BIN_ROOT)/x86_64/$(NAME)
aarch64: $(BIN_ROOT)/aarch64/$(NAME)
arm64-v8a: $(BIN_ROOT)/arm64-v8a/$(NAME)
win64: $(BIN_ROOT)/win64/$(NAME).exe install.exe

$(BIN_ROOT)/x86_64/$(NAME): $(SRC)
	$(MAKE) build_arch ARCH=x86_64 CC="$(CC_x86_64)" EXT=""

$(BIN_ROOT)/aarch64/$(NAME): $(SRC)
	$(MAKE) build_arch ARCH=aarch64 CC="$(CC_aarch64)" EXT=""

$(BIN_ROOT)/arm64-v8a/$(NAME): $(SRC)
	@if [ ! -f "$(CC_arm64_v8a)" ]; then \
		echo "[ERROR] NDK Compiler not found at: $(CC_arm64_v8a)"; \
		exit 1; \
	fi
	$(MAKE) build_arch ARCH=arm64-v8a CC="$(CC_arm64_v8a)" EXT=""

$(BIN_ROOT)/win64/$(NAME).exe: $(SRC)
	$(MAKE) build_arch ARCH=win64 CC="$(CC_win64)" EXT=".exe" \
	CFLAGS="$(CFLAGS) -D_WIN32_WINNT=0x0601"

install.exe: $(INSTALLER_SRC)
	$(CC_win64) $(CFLAGS) -D_WIN32_WINNT=0x0601 -mwindows $(INSTALLER_SRC) -o install.exe $(WININSTALL)

build_arch:
	mkdir -p $(BIN_ROOT)/$(ARCH)
	$(eval SYS_LIB = /usr/local/lib/kaisarcode/$(ARCH))
	$(CC) $(CFLAGS) -c $(SRC) -o $(BIN_ROOT)/$(ARCH)/main.o
	$(CC) $(CFLAGS) $(BIN_ROOT)/$(ARCH)/main.o -o \
	$(BIN_ROOT)/$(ARCH)/$(NAME)$(EXT) \
	$(if $(findstring win64,$(ARCH)),$(WINSOCK))
	@if [ "$(ARCH)" != "win64" ] && command -v patchelf >/dev/null 2>&1; then \
		patchelf --remove-rpath $(BIN_ROOT)/$(ARCH)/$(NAME)$(EXT) || true; \
	fi

clean:
	rm -rf $(BIN_ROOT) install.exe
