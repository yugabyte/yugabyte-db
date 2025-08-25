.PHONY: cargo-exists cargo-pgrx-exists pg_parquet install clean check uninstall

PG_CONFIG ?= pg_config
CARGO_BIN ?= $(shell command -v cargo || echo $(HOME)/.cargo/bin/cargo)
CARGO_DIR := $(dir $(CARGO_BIN))

ifneq (,$(filter clang%,$(YB_COMPILER_TYPE)))
	ifneq ($(origin YB_LLVM_TOOLCHAIN_DIR),undefined)
		CC := $(YB_LLVM_TOOLCHAIN_DIR)/bin/clang
		CXX := $(YB_LLVM_TOOLCHAIN_DIR)/bin/clang++
		RUSTFLAGS := -C link-arg=-fuse-ld=lld -C linker=$(YB_LLVM_TOOLCHAIN_DIR)/bin/clang
		LIBCLANG_PATH := $(YB_LLVM_TOOLCHAIN_DIR)/lib
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
	RUSTFLAGS :=
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
	   cargo clean && \
	   env \
		   $(if $(CC),CC=$(CC),) \
		   $(if $(CXX),CXX=$(CXX),) \
		   $(if $(RUSTFLAGS),RUSTFLAGS="$(RUSTFLAGS)",) \
		   $(if $(LIBCLANG_PATH),LIBCLANG_PATH=$(LIBCLANG_PATH),) \
		   CFLAGS="-Wno-error -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-missing-include-dirs" \
		   cargo build --package cargo-pgrx && \
	   mkdir -p $$YB_BUILD_ROOT/.pgrx/data-15 && \
	   LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
	   PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
	   target/debug/cargo-pgrx pgrx init --pg15 $(PG_CONFIG) && \
	   cd ../pg_parquet

pg_parquet: cargo-pgrx-exists
	   cargo build --release --features pg17

install: cargo-pgrx-exists
	   # YB: Using local cargo-pgrx binary to install pg_parquet extension.
	   export PATH="$(CARGO_DIR):$$PATH" && \
	   cargo clean && \
	   env \
		   $(if $(CC),CC=$(CC),) \
		   $(if $(CXX),CXX=$(CXX),) \
		   $(if $(RUSTFLAGS),RUSTFLAGS="$(RUSTFLAGS)",) \
		   $(if $(LIBCLANG_PATH),LIBCLANG_PATH=$(LIBCLANG_PATH),) \
		   LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
		   CFLAGS="-Wno-error -Wno-implicit-fallthrough -Wno-unused-but-set-variable -Wno-missing-include-dirs" \
		   PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
		   ../pgrx/target/debug/cargo-pgrx pgrx install -F pg15 -c $(PG_CONFIG)

clean: cargo-exists
	   # YB: cleaning pgrx and pg_parquet.
	   cd ../pgrx && \
	   $(CARGO_BIN) clean && \
	   cd ../pg_parquet && \
	   $(CARGO_BIN) clean

check: cargo-pgrx-exists
	   # YB: Using local cargo-pgrx binary to run pg_parquet tests.
	   export PATH="$(CARGO_DIR):$$PATH" && \
	   RUST_TEST_THREADS=1 ../pgrx/target/debug/cargo-pgrx pgrx test pg15

uninstall:
	   rm -f $(shell $(PG_CONFIG) --pkglibdir)/pg_parquet.so
	   rm -f $(shell $(PG_CONFIG) --sharedir)/extension/pg_parquet*
