# Makefile for building and installing the pg_documentdb_gw_host Rust/pgrx extension.
# Shared setup (env vars, clang/gcc detection, cargo-pgrx bootstrap) lives in
# ../../pgrx-common.mk.

.PHONY: all install clean cargo-pgrx-exists

PGRX_DIR := ../../pgrx
# The .cargo/config.toml in pg_documentdb_gw sets target-cpu=x86-64-v3 on x86_64.
# Since the RUSTFLAGS env var overrides config.toml entirely, we have to add it here too.
YB_PGRX_EXTRA_X86_RUSTFLAGS := yes

include ../../pgrx-common.mk

# OpenSSL from thirdparty for the openssl-sys crate.
YB_OPENSSL_DIR := $(YB_THIRDPARTY_DIR)/installed/common

install: cargo-pgrx-exists
	# Using local cargo-pgrx binary to install pg_documentdb_gw_host extension.
	export PATH="$(CARGO_DIR):$$PATH" && \
	$(YB_PGRX_ENV) \
		OPENSSL_DIR=$(YB_OPENSSL_DIR) \
		OPENSSL_STATIC=1 \
		LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
		PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
		$(PGRX_DIR)/target/release/cargo-pgrx pgrx install --release -F pg15 -c $(PG_CONFIG)

clean:
	$(CARGO_BIN) clean
