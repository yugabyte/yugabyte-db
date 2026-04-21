# YB: Shared build configuration for pgrx-based Postgres extensions in YugabyteDB.
#
# Including Makefiles set the following BEFORE `include`:
#   PGRX_DIR                      - relative path to vendored pgrx (required)
#   YB_EXTENSION_CFLAGS           - C warning flags (optional; has default)
#   YB_PGRX_EXTRA_X86_RUSTFLAGS   - set to `yes` to append x86-64-v3 RUSTFLAGS
#
# After include, an extension's recipe can use:
#   $(YB_PGRX_ENV)  - env preamble that unsets parent postgres build env vars
#                     and sets CC/CXX/RUSTFLAGS/CFLAGS/CARGO_INCREMENTAL and
#                     ASAN_OPTIONS for cargo.
# and depend on the `cargo-pgrx-exists` target to bootstrap cargo-pgrx.
#
# Notes on unconditional behavior:
#   - env -u CFLAGS/CXXFLAGS/LDFLAGS/LDFLAGS_EX/CPPFLAGS/PKG_CONFIG_PATH prevents
#     the parent postgres build env vars (containing -Werror, nonexistent include
#     paths like PCRE_INCLUDE_ROOT_DIR-NOTFOUND, etc.) from leaking into Rust
#     *-sys crate compilation (e.g. ring), and avoids cargo cache invalidation
#     caused by changing env var fingerprints on incremental builds.
#   - ASAN_OPTIONS=detect_leaks=0 suppresses false-positive leak reports from
#     cargo and pgrx build tools that block the build in ASAN-enabled environments.

PG_CONFIG ?= pg_config
CARGO_BIN ?= $(shell command -v cargo || echo $(HOME)/.cargo/bin/cargo)
CARGO_DIR := $(dir $(CARGO_BIN))

YB_EXTENSION_CFLAGS ?= -Wno-error -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-missing-include-dirs

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
			YB_EXTENSION_CFLAGS += -isysroot $(YB_SDKROOT)
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
	CC := gcc
	CXX := g++
	RUSTFLAGS := -L$(YB_THIRDPARTY_DIR)/installed/common/lib
	LIBCLANG_PATH := $(if $(YB_LLVM_TOOLCHAIN_DIR),$(YB_LLVM_TOOLCHAIN_DIR)/lib)
endif

# Optional x86_64 RUSTFLAGS. Extensions that vendor a .cargo/config.toml with
# target-cpu settings need these via env too because RUSTFLAGS env var overrides
# the config.toml entirely.
ifeq ($(YB_PGRX_EXTRA_X86_RUSTFLAGS),yes)
	ifeq ($(shell uname -m),x86_64)
		RUSTFLAGS += -C target-cpu=x86-64-v3 -C force-frame-pointers=yes
	endif
endif

# Env preamble for cargo invocations. Recursively-expanded (=) so $(if ...) is
# evaluated at recipe time using the current variable values.
YB_PGRX_ENV = env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u LDFLAGS_EX -u CPPFLAGS -u PKG_CONFIG_PATH \
	$(if $(CC),CC=$(CC),) \
	$(if $(CXX),CXX=$(CXX),) \
	$(if $(RUSTFLAGS),RUSTFLAGS="$(RUSTFLAGS)",) \
	$(if $(LIBCLANG_PATH),LIBCLANG_PATH=$(LIBCLANG_PATH),) \
	CFLAGS="$(YB_EXTENSION_CFLAGS)" \
	CARGO_INCREMENTAL=1 \
	ASAN_OPTIONS=detect_leaks=0

all:

# Build cargo-pgrx into $(PGRX_DIR)/target/release/cargo-pgrx and initialize
# the pgrx home. Idempotent: if the config.toml already references the right
# PG_CONFIG, the init step is skipped.
cargo-pgrx-exists:
	# YB: Building cargo-pgrx and configuring pgrx.
	# YB: Creating empty data directory `data-15` to skip running 'initdb'.
	export PATH="$(CARGO_DIR):$$PATH" && \
	cd $(PGRX_DIR) && \
	$(YB_PGRX_ENV) cargo build --package cargo-pgrx --release && \
	mkdir -p $$YB_BUILD_ROOT/.pgrx/data-15 && \
	if [ ! -f "$(YB_BUILD_ROOT)/.pgrx/config.toml" ] || ! grep -q "^pg15\s*=\s*\"$$PG_CONFIG\"" "$(YB_BUILD_ROOT)/.pgrx/config.toml" >/dev/null 2>&1; then \
		LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
		PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
		ASAN_OPTIONS=detect_leaks=0 \
		target/release/cargo-pgrx pgrx init --pg15 $(PG_CONFIG); \
	fi
