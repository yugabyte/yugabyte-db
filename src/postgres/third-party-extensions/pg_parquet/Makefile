.PHONY: cargo-exists cargo-pgrx-exists pg_parquet install clean check uninstall

PG_CONFIG ?= pg_config
CARGO_BIN ?= $(shell command -v cargo || echo $(HOME)/.cargo/bin/cargo)
CARGO_DIR := $(dir $(CARGO_BIN))

YB_PG_PARQUET_CFLAGS := -Wno-error -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-missing-include-dirs

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
			YB_PG_PARQUET_CFLAGS += -isysroot $(YB_SDKROOT)
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

all:

cargo-exists:
	   $(CARGO_BIN) --version > /dev/null || (echo "cargo is not available. Please install cargo." && exit 1)

cargo-pgrx-exists: cargo-exists
	   # YB: Building cargo-pgrx and configuring pgrx.
	   # YB: Creating empty data directory `data-15` to skip running 'initdb'
	   export PATH="$(CARGO_DIR):$$PATH" && \
	   cd ../pgrx && \
	   env \
		   $(if $(CC),CC=$(CC),) \
		   $(if $(CXX),CXX=$(CXX),) \
		   $(if $(RUSTFLAGS),RUSTFLAGS="$(RUSTFLAGS)",) \
		   $(if $(LIBCLANG_PATH),LIBCLANG_PATH=$(LIBCLANG_PATH),) \
		   CFLAGS="$(YB_PG_PARQUET_CFLAGS)" \
		   CARGO_INCREMENTAL=1 \
		   cargo build --package cargo-pgrx --release && \
	   mkdir -p $$YB_BUILD_ROOT/.pgrx/data-15 && \
	   if [ ! -f "$(YB_BUILD_ROOT)/.pgrx/config.toml" ] || ! grep -q "^pg15\s*=\s*\"$$PG_CONFIG\"" "$(YB_BUILD_ROOT)/.pgrx/config.toml" >/dev/null 2>&1; then \
		   LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
		   PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
		   target/release/cargo-pgrx pgrx init --pg15 $(PG_CONFIG); \
	   fi && \
	   cd ../pg_parquet

pg_parquet: cargo-pgrx-exists
	   cargo build --release --features pg17

install: cargo-pgrx-exists
	   # YB: Using local cargo-pgrx binary to install pg_parquet extension.
	   export PATH="$(CARGO_DIR):$$PATH" && \
	   env \
		   $(if $(CC),CC=$(CC),) \
		   $(if $(CXX),CXX=$(CXX),) \
		   $(if $(RUSTFLAGS),RUSTFLAGS="$(RUSTFLAGS)",) \
		   $(if $(LIBCLANG_PATH),LIBCLANG_PATH=$(LIBCLANG_PATH),) \
		   CFLAGS="$(YB_PG_PARQUET_CFLAGS)" \
		   LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
		   PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
		   CARGO_INCREMENTAL=1 \
		   ../pgrx/target/release/cargo-pgrx pgrx install --release -F pg15 -c $(PG_CONFIG) && \
	   [ "$$YB_PG_PARQUET_DEBUG_CLEAN" = "1" ] && rm -rf target/debug || true

clean: cargo-exists
	   # YB: cleaning pgrx and pg_parquet.
	   cd ../pgrx && \
	   $(CARGO_BIN) clean && \
	   cd ../pg_parquet && \
	   $(CARGO_BIN) clean

check: cargo-pgrx-exists
	   # YB: Using local cargo-pgrx binary to run pg_parquet tests.
	   export PATH="$(CARGO_DIR):$$PATH" && \
	   RUST_TEST_THREADS=1 ../pgrx/target/release/cargo-pgrx pgrx test pg15

uninstall:
	   rm -f $(shell $(PG_CONFIG) --pkglibdir)/pg_parquet.so
	   rm -f $(shell $(PG_CONFIG) --sharedir)/extension/pg_parquet*
