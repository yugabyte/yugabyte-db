# Makefile for building and installing the pg_documentdb_gw_host Rust/pgrx extension.
# Modeled after src/postgres/third-party-extensions/pg_parquet/Makefile.
#
# YB: Toolchain detection, cargo env scrubbing, and PG_CONFIG/CARGO_BIN live in
# the shared pgrx fragment. cargo-pgrx itself is built by the pgrx directory,
# ordered ahead of this directory by the parent Makefile. The $(YB_CARGO_PGRX) rule
# below bootstraps it for direct invocations only.

.PHONY: all install clean

include ../../pgrx/pgrx-common.mk

# Include flags from .cargo/config.toml since RUSTFLAGS env var overrides them.
# The .cargo/config.toml sets these only for x86_64, so gate them here too.
# These apply to this extension only, not to the shared cargo-pgrx build.
ifeq ($(shell uname -m),x86_64)
	RUSTFLAGS += -C target-cpu=x86-64-v3 -C force-frame-pointers=yes
endif

# YB: `all` stays the first target so it remains the default goal: the parent
# Makefile invokes this directory with no target, and a file rule first would
# make the build phase bootstrap cargo-pgrx.
all:

# YB: Direct invocations bootstrap cargo-pgrx themselves. Under the parent
# Makefile install-pgrx has already built it, so this file rule does not fire.
$(YB_CARGO_PGRX):
	$(MAKE) -C $(YB_PGRX_DIR) cargo-pgrx

# OpenSSL from thirdparty for the openssl-sys crate.
# The common dir has headers+libs; fall back to uninstrumented if common doesn't have them.
YB_OPENSSL_DIR := $(YB_THIRDPARTY_DIR)/installed/common

install: $(YB_CARGO_PGRX)
	export PATH="$(CARGO_DIR):$$PATH" && \
	$(YB_PGRX_CARGO_ENV) \
		OPENSSL_DIR=$(YB_OPENSSL_DIR) \
		OPENSSL_STATIC=1 \
		$(YB_PGRX_RUN_ENV) \
		$(YB_CARGO_PGRX) pgrx install --release -F pg15 -c $(PG_CONFIG)

clean:
	$(CARGO_BIN) clean
