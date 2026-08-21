# Makefile for building and installing the pg_documentdb_gw_host Rust/pgrx extension.
# Modeled after src/postgres/third-party-extensions/pg_parquet/Makefile.
#
# NOTE: ASAN_OPTIONS=detect_leaks=0 is set for all cargo/pgrx invocations because
# cargo and pgrx build tools trigger false positive leak reports from Rust's
# allocator and build-time codegen that block the build in ASAN-enabled environments.
#
# NOTE: Inherited env vars (CFLAGS, CXXFLAGS, LDFLAGS, etc.) from the parent postgres
# build are unset via env -u before cargo invocations. These vars contain -Werror,
# nonexistent include paths (PCRE_INCLUDE_ROOT_DIR-NOTFOUND), and other flags that
# break Rust *-sys crate compilation (e.g. ring) and cause unnecessary cargo cache
# invalidation on incremental builds.

.PHONY: all install clean cargo-pgrx-exists

PG_CONFIG ?= pg_config
CARGO_BIN ?= $(shell command -v cargo || echo $(HOME)/.cargo/bin/cargo)
CARGO_DIR := $(dir $(CARGO_BIN))

# Path to the vendored pgrx directory (relative to this Makefile).
PGRX_DIR := ../../pgrx

YB_GW_HOST_CFLAGS := -Wno-error -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-missing-include-dirs

# OS detection
YB_UNAME_S := $(shell uname -s)

# macOS SDK (only defined on Darwin)
ifeq ($(YB_UNAME_S),Darwin)
	YB_SDKROOT := $(shell xcrun --show-sdk-path)
endif

ifneq (,$(filter clang%,$(YB_COMPILER_TYPE)))
	ifneq ($(origin YB_LLVM_TOOLCHAIN_DIR),undefined)
		CC := $(YB_LLVM_TOOLCHAIN_DIR)/bin/clang
		CXX := $(YB_LLVM_TOOLCHAIN_DIR)/bin/clang++
		RUSTFLAGS := -C link-arg=-fuse-ld=lld -C linker=$(YB_LLVM_TOOLCHAIN_DIR)/bin/clang
		LIBCLANG_PATH := $(YB_LLVM_TOOLCHAIN_DIR)/lib

		ifeq ($(YB_UNAME_S),Darwin)
			RUSTFLAGS += -C link-arg=-isysroot -C link-arg=$(YB_SDKROOT)
			RUSTFLAGS += -C link-arg=-Wl,-undefined,dynamic_lookup
			YB_GW_HOST_CFLAGS += -isysroot $(YB_SDKROOT)
		else
			RUSTFLAGS += -L$(YB_THIRDPARTY_DIR)/installed/common/lib
		endif
		ifeq ($(YB_BUILD_TYPE),tsan)
			RUSTFLAGS += -C link-arg=-fsanitize=thread
			RUSTFLAGS += -C link-arg=$(shell $(YB_LLVM_TOOLCHAIN_DIR)/bin/clang++ -print-file-name=libclang_rt.tsan.so)
		endif
		ifeq ($(YB_BUILD_TYPE),asan)
			RUSTFLAGS += -C link-arg=-fsanitize=address
			RUSTFLAGS += -C link-arg=$(shell $(YB_LLVM_TOOLCHAIN_DIR)/bin/clang++ -print-file-name=libclang_rt.asan.so)
		endif
	endif
else
	RUSTFLAGS := -L$(YB_THIRDPARTY_DIR)/installed/common/lib
	LIBCLANG_PATH := $(if $(YB_LLVM_TOOLCHAIN_DIR),$(YB_LLVM_TOOLCHAIN_DIR)/lib)
	ifneq ($(origin YB_GCC_PREFIX),undefined)
		CC := $(YB_GCC_PREFIX)/bin/gcc$(YB_GCC_SUFFIX)
		CXX := $(YB_GCC_PREFIX)/bin/g++$(YB_GCC_SUFFIX)
		RUSTFLAGS += -C linker=$(CC)
	else
		CC := gcc
		CXX := g++
	endif
endif

# Include flags from .cargo/config.toml since RUSTFLAGS env var overrides them.
# The .cargo/config.toml sets these only for x86_64, so gate them here too.
ifeq ($(shell uname -m),x86_64)
	RUSTFLAGS += -C target-cpu=x86-64-v3 -C force-frame-pointers=yes
endif

all:

# OpenSSL from thirdparty for the openssl-sys crate.
# The common dir has headers+libs; fall back to uninstrumented if common doesn't have them.
YB_OPENSSL_DIR := $(YB_THIRDPARTY_DIR)/installed/common

cargo-pgrx-exists:
	# Building cargo-pgrx and configuring pgrx.
	# Creating empty data directory `data-15` to skip running 'initdb'
	export PATH="$(CARGO_DIR):$$PATH" && \
	cd $(PGRX_DIR) && \
	env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u LDFLAGS_EX -u CPPFLAGS -u PKG_CONFIG_PATH \
		$(if $(CC),CC=$(CC),) \
		$(if $(CXX),CXX=$(CXX),) \
		$(if $(RUSTFLAGS),RUSTFLAGS="$(RUSTFLAGS)",) \
		$(if $(LIBCLANG_PATH),LIBCLANG_PATH=$(LIBCLANG_PATH),) \
		CFLAGS="$(YB_GW_HOST_CFLAGS)" \
		CARGO_INCREMENTAL=1 \
		ASAN_OPTIONS=detect_leaks=0 \
		cargo build --package cargo-pgrx --release && \
	mkdir -p $$YB_BUILD_ROOT/.pgrx/data-15 && \
	if [ ! -f "$(YB_BUILD_ROOT)/.pgrx/config.toml" ] || ! grep -q "^pg15\s*=\s*\"$$PG_CONFIG\"" "$(YB_BUILD_ROOT)/.pgrx/config.toml" >/dev/null 2>&1; then \
		LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
		PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
		ASAN_OPTIONS=detect_leaks=0 \
		target/release/cargo-pgrx pgrx init --pg15 $(PG_CONFIG); \
	fi

install: cargo-pgrx-exists
	# Using local cargo-pgrx binary to install pg_documentdb_gw_host extension.
	export PATH="$(CARGO_DIR):$$PATH" && \
	env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u LDFLAGS_EX -u CPPFLAGS -u PKG_CONFIG_PATH \
		$(if $(CC),CC=$(CC),) \
		$(if $(CXX),CXX=$(CXX),) \
		$(if $(RUSTFLAGS),RUSTFLAGS="$(RUSTFLAGS)",) \
		$(if $(LIBCLANG_PATH),LIBCLANG_PATH=$(LIBCLANG_PATH),) \
		CFLAGS="$(YB_GW_HOST_CFLAGS)" \
		OPENSSL_DIR=$(YB_OPENSSL_DIR) \
		OPENSSL_STATIC=1 \
		LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
		PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
		CARGO_INCREMENTAL=1 \
		ASAN_OPTIONS=detect_leaks=0 \
		$(PGRX_DIR)/target/release/cargo-pgrx pgrx install --release -F pg15 -c $(PG_CONFIG)

clean:
	$(CARGO_BIN) clean
