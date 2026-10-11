# YB: Shared make settings for the vendored pgrx framework and its consumers,
# included by pgrx/Makefile, pg_parquet/Makefile and
# documentdb/pg_documentdb_gw_host/Makefile.
#
# NOTE: Inherited env vars (CFLAGS, CXXFLAGS, LDFLAGS, etc.) from the parent
# postgres build are unset via `env -u` before every cargo invocation. They
# contain -Werror, nonexistent include paths (PCRE_INCLUDE_ROOT_DIR-NOTFOUND),
# and other flags that break Rust *-sys crate compilation (e.g. ring). They are
# also large and change between builds, which makes cargo consider every *-sys
# crate dirty and recompile the whole HTTP stack (ring, rustls, reqwest,
# zstd-sys, openssl-sys, bzip2-sys, ...), adding ~30s per incremental build.
#
# NOTE: ASAN_OPTIONS=detect_leaks=0 is set for all cargo/pgrx invocations
# because cargo and the pgrx build tools trigger false positive leak reports
# from Rust's allocator and build-time codegen that block the build in ASAN
# environments.

# YB: Directory holding this fragment, i.e. the vendored pgrx tree, resolved
# relative to whichever Makefile included it.
YB_PGRX_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
YB_CARGO_PGRX := $(YB_PGRX_DIR)/target/release/cargo-pgrx

PG_CONFIG ?= pg_config
CARGO_BIN ?= $(shell command -v cargo || echo $(HOME)/.cargo/bin/cargo)
CARGO_DIR := $(dir $(CARGO_BIN))

YB_PGRX_CFLAGS := -Wno-error -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-missing-include-dirs

# YB: OS detection
YB_UNAME_S := $(shell uname -s)

# YB: macOS SDK (only defined on Darwin)
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
			YB_PGRX_CFLAGS += -isysroot $(YB_SDKROOT)
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

# YB: Env prefix for cargo / cargo-pgrx invocations.  Recursively expanded (`=`,
# not `:=`) so that a consumer appending to RUSTFLAGS after including this file
# still takes effect.
YB_PGRX_CARGO_ENV = env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u LDFLAGS_EX -u CPPFLAGS -u PKG_CONFIG_PATH \
	$(if $(CC),CC=$(CC),) \
	$(if $(CXX),CXX=$(CXX),) \
	$(if $(RUSTFLAGS),RUSTFLAGS="$(RUSTFLAGS)",) \
	$(if $(LIBCLANG_PATH),LIBCLANG_PATH=$(LIBCLANG_PATH),) \
	CFLAGS="$(YB_PGRX_CFLAGS)" \
	CARGO_INCREMENTAL=1 \
	ASAN_OPTIONS=detect_leaks=0

# YB: Runtime env every cargo-pgrx invocation needs on top of
# YB_PGRX_CARGO_ENV: the thirdparty runtime libs and the shared PGRX_HOME.
YB_PGRX_RUN_ENV = LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
	PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx
