# Makefile for the led-matrix-server app.
#
# This app only ever runs on a (64-bit) Raspberry Pi, so `make` targets aarch64:
#   On a Pi (uname -m = aarch64):     make   -> native build
#   On any other host (e.g. x86_64):  make   -> auto cross-compile to aarch64
# There is deliberately no way to produce an x86 binary; a non-aarch64 `make`
# transparently routes through the cross path below.
#
# `make cross` downloads the ARM-official aarch64 toolchain into toolchain/
# on first use (no apt packages needed), then re-invokes make with CROSS=
# pointing at it. A system toolchain still works: make CROSS=aarch64-linux-gnu-
#
# The hub75 driver library (rpi-rgb-led-matrix) is cloned automatically into
# a subdirectory if it isn't there yet, and its static library
# (rpi-rgb-led-matrix/lib/librgbmatrix.a) is (re)built with the same
# compiler so the object files match the target architecture.

CROSS ?=

# Host architecture. On a 64-bit Raspberry Pi OS this is "aarch64"; anything
# else means we're on a foreign host and must cross-compile.
HOST_ARCH := $(shell uname -m)
CXX := $(CROSS)g++
CC  := $(CROSS)gcc
AR  := $(CROSS)gcc-ar          # LTO archiving needs the matching gcc-ar wrapper

ifeq ($(CROSS),)
  ARCH_FLAGS := -march=native -mtune=native
  # Native build (on the Pi): glibc matches, link dynamically.
  STATIC :=
else
  ARCH_FLAGS :=
  # Cross build: the host glibc may be newer than the Pi's, so newly
  # versioned math symbols (atan2f/sqrtf@GLIBC_2.43, etc.) would fail to
  # resolve on the Pi. Link statically so the binary carries its own libc.
  STATIC := -static
endif

CXXFLAGS := -Wall -Wextra -Wno-unused-parameter -O3 -g -std=c++11 -pthread $(ARCH_FLAGS) -flto=2 -MMD -MP
LDFLAGS  := $(ARCH_FLAGS) -flto=2 -pthread $(STATIC) -lrt -lm -lpthread

# --- Optional HTTPS support (TLS=1) -----------------------------------------
# `make TLS=1` cross-builds a static aarch64 OpenSSL once (cached in openssl/,
# like the toolchain) and links it in, so the binary stays self-contained and
# HTTPS-capable. Off by default: the normal build has no OpenSSL dependency and
# is byte-for-byte what it was. At runtime HTTPS is still opt-in (needs -c/-k).
ifdef TLS
OPENSSL_VERSION := 3.0.15
OPENSSL_DIR     := openssl
OPENSSL_SRC     := $(OPENSSL_DIR)/openssl-$(OPENSSL_VERSION)
OPENSSL_PREFIX  := $(abspath $(OPENSSL_DIR)/install)
OPENSSL_LIB     := $(OPENSSL_PREFIX)/lib/libssl.a
OPENSSL_URL     := https://github.com/openssl/openssl/releases/download/openssl-$(OPENSSL_VERSION)/openssl-$(OPENSSL_VERSION).tar.gz
CXXFLAGS += -DUSE_TLS -I$(OPENSSL_PREFIX)/include
# libssl needs libcrypto, which needs -ldl (provider loading is disabled but the
# default provider still links it). Placed before the system libs in LDFLAGS.
TLS_LIBS := -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto -ldl
endif

RGB_LIB_DISTRIBUTION := rpi-rgb-led-matrix
RGB_REPO := https://github.com/hzeller/rpi-rgb-led-matrix.git
RGB_INCDIR := $(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR := $(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY := $(RGB_LIBDIR)/librgbmatrix.a

BINARY := led-matrix-server
CALBIN := panel-cal

# App sources live in src/, the calibration tool in calibration/.
SRC_DIR := src
CAL_DIR := calibration

# Vendored Lua 5.4 (embedded interpreter for the Lua mode). Plain C, POSIX,
# no dlopen (safe for the static build). Built without LTO; links fine anyway.
LUA_DIR := $(SRC_DIR)/lua
LUA_SRCS := $(wildcard $(LUA_DIR)/*.c)
LUA_OBJS := $(LUA_SRCS:.c=.o)
LUA_CFLAGS := -O2 $(ARCH_FLAGS) -DLUA_USE_POSIX -I$(LUA_DIR)

# One translation unit per display mode: cpp_modes/<name>/<name>.cc, each
# compiled to its own .o beside its source. Adding a mode means adding a
# directory -- it is picked up here and registers itself at startup (see
# REGISTER_MODE in src/common/mode.h), with no central list to edit. (The Lua
# side is a sibling tree, lua/<name>/<name>.lua -- runtime-fetched scripts, not
# compiled; see the catalog server.)
COMMON_SRCS := $(wildcard $(SRC_DIR)/common/*.cc)
COMMON_OBJS := $(COMMON_SRCS:.cc=.o)
MODE_SRCS := $(wildcard cpp_modes/*/*.cc)
MODE_OBJS := $(MODE_SRCS:.cc=.o)
# Runtime-fetched Lua modules (catalog fetch/parse + the mode wrapper).
MODULE_SRCS := $(wildcard $(SRC_DIR)/modules/*.cc)
MODULE_OBJS := $(MODULE_SRCS:.cc=.o)
APP_OBJS := led-matrix-server.o $(COMMON_OBJS) $(MODE_OBJS) $(MODULE_OBJS)

# Header search path shared by every app TU: repo root (for cpp_modes/...),
# src/ (for common/... and modules/...), plus the vendored lib + Lua headers.
APP_INCLUDES := -I. -I$(SRC_DIR) -I$(RGB_INCDIR) -I$(LUA_DIR)

# Default target. If we're not already cross-compiling (CROSS unset) and not on
# an aarch64 host, delegate the whole build to the cross path so we never emit
# an x86 binary. `cross` re-invokes make with CROSS set, and that sub-make comes
# back through here with aarch64-targeting flags to do the real build.
ifeq ($(CROSS),)
ifneq ($(HOST_ARCH),aarch64)
all: cross
	@echo ">> Non-Pi host ($(HOST_ARCH)) -> built aarch64 binary via cross-compile."
else
all : $(BINARY) $(CALBIN)
endif
else
all : $(BINARY) $(CALBIN)
endif

# --- Cross toolchain (fetched on demand) ------------------------------------
# ARM's prebuilt aarch64-none-linux-gnu toolchain, extracted into toolchain/.
# The tarball is ~160 MB; it is fetched once and never cleaned by clean/
# distclean (rm -rf toolchain/ to get rid of it).
TOOLCHAIN_VERSION := 14.2.rel1
TOOLCHAIN_TRIPLE  := aarch64-none-linux-gnu
TOOLCHAIN_NAME    := arm-gnu-toolchain-$(TOOLCHAIN_VERSION)-x86_64-$(TOOLCHAIN_TRIPLE)
TOOLCHAIN_URL     := https://developer.arm.com/-/media/Files/downloads/gnu/$(TOOLCHAIN_VERSION)/binrel/$(TOOLCHAIN_NAME).tar.xz
TOOLCHAIN_DIR     := toolchain
TOOLCHAIN_BIN     := $(TOOLCHAIN_DIR)/$(TOOLCHAIN_NAME)/bin
TOOLCHAIN_GXX     := $(TOOLCHAIN_BIN)/$(TOOLCHAIN_TRIPLE)-g++

$(TOOLCHAIN_GXX):
	mkdir -p $(TOOLCHAIN_DIR)
	curl -fL -o $(TOOLCHAIN_DIR)/$(TOOLCHAIN_NAME).tar.xz "$(TOOLCHAIN_URL)"
	tar -C $(TOOLCHAIN_DIR) -xf $(TOOLCHAIN_DIR)/$(TOOLCHAIN_NAME).tar.xz
	rm -f $(TOOLCHAIN_DIR)/$(TOOLCHAIN_NAME).tar.xz

# Absolute CROSS prefix: the library is built via $(MAKE) -C, so a relative
# path would break inside the sub-make's working directory.
cross: $(TOOLCHAIN_GXX)
	$(MAKE) CROSS=$(abspath $(TOOLCHAIN_BIN))/$(TOOLCHAIN_TRIPLE)-

# Clone the driver library if the subdirectory isn't there yet. The lib
# Makefile doubles as the "clone happened" marker.
$(RGB_LIBDIR)/Makefile:
	git clone $(RGB_REPO) $(RGB_LIB_DISTRIBUTION)

$(RGB_LIBRARY): $(RGB_LIBDIR)/Makefile FORCE
	$(MAKE) -C $(RGB_LIBDIR) CXX="$(CXX)" CC="$(CC)" AR="$(AR)" \
		CPU_ARCH_FLAGS="$(ARCH_FLAGS)"

# The app objects include headers from the cloned tree, so make sure the
# clone exists before compiling (order-only: don't rebuild on lib changes).
$(APP_OBJS) panel-cal.o: | $(RGB_LIBDIR)/Makefile

$(BINARY) : $(APP_OBJS) $(LUA_OBJS) $(RGB_LIBRARY)
	$(CXX) $(APP_OBJS) $(LUA_OBJS) -o $@ -L$(RGB_LIBDIR) -lrgbmatrix $(TLS_LIBS) $(LDFLAGS)

# Static aarch64 OpenSSL, built once and cached (like the toolchain; not removed
# by clean/distclean -- rm -rf openssl/ to rebuild). no-module/no-dso keep it
# free of dlopen so it links into the fully-static binary.
ifdef TLS
$(OPENSSL_LIB):
	mkdir -p $(OPENSSL_DIR)
	cd $(OPENSSL_DIR) && curl -fL -o openssl.tar.gz "$(OPENSSL_URL)" && \
		tar xf openssl.tar.gz && rm -f openssl.tar.gz
	cd $(OPENSSL_SRC) && ./Configure linux-aarch64 \
		no-shared no-module no-dso no-tests no-engine \
		$(if $(CROSS),--cross-compile-prefix=$(CROSS)) \
		--prefix=$(OPENSSL_PREFIX) --libdir=lib
	$(MAKE) -C $(OPENSSL_SRC) -j$(shell nproc)
	$(MAKE) -C $(OPENSSL_SRC) install_sw
# App objects need OpenSSL's headers present first; the binary needs the lib.
$(APP_OBJS): | $(OPENSSL_LIB)
$(BINARY): $(OPENSSL_LIB)
endif

led-matrix-server.o : $(SRC_DIR)/led-matrix-server.cc
	$(CXX) $(APP_INCLUDES) $(CXXFLAGS) -c -o $@ $<

# The shared framework, the C++ modes (cpp_modes/), and the fetched-module
# subsystem. All share APP_INCLUDES so a "common/..." or "cpp_modes/..." include
# resolves the same wherever the TU lives.
$(SRC_DIR)/common/%.o : $(SRC_DIR)/common/%.cc
	$(CXX) $(APP_INCLUDES) $(CXXFLAGS) -c -o $@ $<

cpp_modes/%.o : cpp_modes/%.cc
	$(CXX) $(APP_INCLUDES) $(CXXFLAGS) -c -o $@ $<

$(SRC_DIR)/modules/%.o : $(SRC_DIR)/modules/%.cc
	$(CXX) $(APP_INCLUDES) $(CXXFLAGS) -c -o $@ $<

$(LUA_DIR)/%.o : $(LUA_DIR)/%.c
	$(CC) $(LUA_CFLAGS) -c -o $@ $<

$(CALBIN) : panel-cal.o $(RGB_LIBRARY)
	$(CXX) panel-cal.o -o $@ -L$(RGB_LIBDIR) -lrgbmatrix $(LDFLAGS)

panel-cal.o : $(CAL_DIR)/panel-cal.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -c -o $@ $<

# Generated by -MMD: one .d of header dependencies per object.
DEPS := $(APP_OBJS:.o=.d) panel-cal.d
-include $(DEPS)

clean:
	rm -f $(APP_OBJS) $(DEPS) $(BINARY) panel-cal.o $(CALBIN) $(LUA_OBJS)

# Also clean the library objects, so switching between native and cross
# builds never links mismatched architectures.
distclean: clean
	if [ -d $(RGB_LIBDIR) ]; then $(MAKE) -C $(RGB_LIBDIR) clean; fi

# --- Deploy to the Pi (override target with PI_HOST / PI_USER / PI_KEY / PI_DEST) --
PI_USER ?= root
PI_HOST ?= 172.16.16.168
PI_KEY  ?= $(HOME)/.ssh/id_ed25519
PI_DEST ?= led-matrix-server
SSH := ssh -i $(PI_KEY) -o ConnectTimeout=10
SCP := scp -i $(PI_KEY) -o ConnectTimeout=10
PI := $(PI_USER)@$(PI_HOST)

# Copy the (cross-built) binary, config and fonts to the Pi. Upload the
# executables to a temp name then atomically mv over, so a running service
# (which holds the old inode) doesn't fail with "text file busy".
deploy:
	@test -x $(BINARY) || { echo "error: $(BINARY) not built -- run 'make cross' first." >&2; exit 1; }
	$(SSH) $(PI) "mkdir -p ~/$(PI_DEST)/fonts"
	$(SCP) $(BINARY) $(PI):~/$(PI_DEST)/$(BINARY).new
	$(SSH) $(PI) "chmod +x ~/$(PI_DEST)/$(BINARY).new && mv -f ~/$(PI_DEST)/$(BINARY).new ~/$(PI_DEST)/$(BINARY)"
	$(SCP) led-matrix-server.conf $(PI):~/$(PI_DEST)/
	@if [ -x $(CALBIN) ]; then \
	  $(SCP) $(CALBIN) $(PI):~/$(PI_DEST)/$(CALBIN).new; \
	  $(SSH) $(PI) "chmod +x ~/$(PI_DEST)/$(CALBIN).new && mv -f ~/$(PI_DEST)/$(CALBIN).new ~/$(PI_DEST)/$(CALBIN)"; \
	fi
	$(SCP) rpi-rgb-led-matrix/fonts/6x10.bdf rpi-rgb-led-matrix/fonts/5x7.bdf $(PI):~/$(PI_DEST)/fonts/
	@echo ">> Deployed. Restart: $(SSH) $(PI) systemctl restart led-matrix-server"

# Install/enable the systemd unit (run 'make deploy' first).
install-service:
	$(SCP) led-matrix-server.service $(PI):/etc/systemd/system/led-matrix-server.service
	$(SSH) $(PI) "systemctl daemon-reload && systemctl enable --now led-matrix-server.service"

FORCE:
.PHONY: FORCE all cross clean distclean deploy install-service
