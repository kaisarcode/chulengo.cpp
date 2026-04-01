# chulengo - Multi-architecture Makefile
# Summary: Builds the raw chulengo binary and installer payloads.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

NAME = chulengo
INSTALLER_SRC = src/win/install.c
BIN_ROOT = bin
DEPS_ROOT = /usr/local/lib/kaisarcode
TOOLCHAIN_ROOT = /usr/local/share/kaisarcode/toolchains
SRC = src/main.c src/core.c src/core.h src/pal.h

INC_DIR = $(DEPS_ROOT)/inc/llama.cpp
GGML_INC = $(DEPS_ROOT)/inc/ggml
LIB_ROOT = $(DEPS_ROOT)/obj/llama.cpp/$(ARCH)
GGML_LIB = $(DEPS_ROOT)/obj/ggml/$(ARCH)
MTMD_LIB = $(LIB_ROOT)/libmtmd.so

CC_x86_64 = gcc
CC_aarch64 = aarch64-linux-gnu-gcc
NDK_VER = android-ndk-r27c
NDK_HOST = linux-x86_64
NDK_ROOT = $(TOOLCHAIN_ROOT)/ndk/$(NDK_VER)
NDK_BIN = $(NDK_ROOT)/toolchains/llvm/prebuilt/$(NDK_HOST)/bin
NDK_API = 24
CC_arm64_v8a = $(NDK_BIN)/aarch64-linux-android$(NDK_API)-clang
CC_win64 = x86_64-w64-mingw32-gcc

CFLAGS = -Wall -Wextra -Werror -O3 -std=c11 -I$(INC_DIR) -I$(GGML_INC) $(EXTRA_CFLAGS)
LDLIBS = -lllama -lggml -lggml-base -lggml-cpu
WINSOCK = -lws2_32 -ladvapi32 -Wl,--no-insert-timestamp
WININSTALL = -lurlmon -lshell32 -ladvapi32 -lshlwapi -lcomctl32 -Wl,--no-insert-timestamp

.PHONY: all clean build_arch x86_64 aarch64 arm64-v8a win64

all: x86_64 aarch64 arm64-v8a win64

x86_64: $(BIN_ROOT)/x86_64/$(NAME)

$(BIN_ROOT)/x86_64/$(NAME): $(SRC)
	$(MAKE) build_arch ARCH=x86_64 CC="$(CC_x86_64)" EXT="" EXTRA_LDLIBS="-pthread"

aarch64: $(BIN_ROOT)/aarch64/$(NAME)

$(BIN_ROOT)/aarch64/$(NAME): $(SRC)
	$(MAKE) build_arch ARCH=aarch64 CC="$(CC_aarch64)" EXT="" EXTRA_LDLIBS="-pthread"

arm64-v8a: $(BIN_ROOT)/arm64-v8a/$(NAME)

$(BIN_ROOT)/arm64-v8a/$(NAME): $(SRC)
	@if [ ! -f "$(CC_arm64_v8a)" ]; then \
		echo "[ERROR] NDK Compiler not found at: $(CC_arm64_v8a)"; \
		exit 1; \
	fi
	$(MAKE) build_arch ARCH=arm64-v8a CC="$(CC_arm64_v8a)" EXT=""

win64: $(BIN_ROOT)/win64/$(NAME).exe install.exe

$(BIN_ROOT)/win64/$(NAME).exe: $(SRC)
	$(MAKE) build_arch ARCH=win64 CC="$(CC_win64)" EXT=".exe" EXTRA_CFLAGS="-D_WIN32_WINNT=0x0601" EXTRA_LDLIBS="$(WINSOCK)"

install.exe: $(INSTALLER_SRC)
	$(CC_win64) $(CFLAGS) -D_WIN32_WINNT=0x0601 -mwindows $(INSTALLER_SRC) -o install.exe $(WININSTALL)

build_arch:
	mkdir -p $(BIN_ROOT)/$(ARCH)
	$(eval UNIX_LIBS = $(LIB_ROOT)/libllama.so $(GGML_LIB)/libggml.so $(GGML_LIB)/libggml-base.so $(GGML_LIB)/libggml-cpu.so $(if $(wildcard $(MTMD_LIB)),$(MTMD_LIB),) -lm)
	$(eval UNIX_RPATH = -Wl,-rpath,$(LIB_ROOT) -Wl,-rpath,$(GGML_LIB))
	$(eval WIN_LIBS = -L$(LIB_ROOT) -L$(GGML_LIB) $(LDLIBS))
	$(eval MTMD_CFLAGS = $(if $(wildcard $(MTMD_LIB)),-DCHULENGO_HAVE_MTMD=1,))
	$(eval OBJS = $(BIN_ROOT)/$(ARCH)/main.o $(BIN_ROOT)/$(ARCH)/core.o)
	$(MAKE) $(OBJS) ARCH=$(ARCH) CC="$(CC)" EXT="$(EXT)" EXTRA_CFLAGS="$(EXTRA_CFLAGS) $(MTMD_CFLAGS)"
	$(CC) $(CFLAGS) $(MTMD_CFLAGS) $(OBJS) -o $(BIN_ROOT)/$(ARCH)/$(NAME)$(EXT) $(if $(findstring win64,$(ARCH)),$(WIN_LIBS) $(EXTRA_LDLIBS),$(UNIX_LIBS) $(UNIX_RPATH) $(EXTRA_LDLIBS))

$(BIN_ROOT)/$(ARCH)/%.o: src/%.c
	mkdir -p $(BIN_ROOT)/$(ARCH)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN_ROOT) install.exe
