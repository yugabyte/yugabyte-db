# YB: Build-time support for the vendored pgrx framework.
#
# pgrx is a framework, not a PostgreSQL extension: nothing here is installed
# into the PostgreSQL tree. This directory exists so that cargo-pgrx is built
# and PGRX_HOME initialized exactly once, before any consumer
# (pg_parquet, documentdb/pg_documentdb_gw_host) needs them. The parent
# Makefile orders this directory ahead of those consumers.
#
# This file and pgrx-common.mk are YB additions to a vendored upstream tree
# (src/lint/upstream_repositories.csv). Re-importing pgrx replaces the tree with
# upstream's contents, which has neither file -- carry both across, or every
# consumer loses its build.

.PHONY: all install clean cargo-exists cargo-pgrx

include pgrx-common.mk

# YB: The build phase deliberately does nothing. cargo-pgrx is needed at install
# time only, and keeping `all` empty is what keeps plain `make` off cargo.
all:

cargo-exists:
	$(CARGO_BIN) --version > /dev/null || (echo "cargo is not available. Please install cargo." && exit 1)

# YB: `data-15` is created empty so that `pgrx init` skips running initdb.
cargo-pgrx: cargo-exists
	export PATH="$(CARGO_DIR):$$PATH" && \
	$(YB_PGRX_CARGO_ENV) \
		cargo build --package cargo-pgrx --release && \
	mkdir -p $(YB_BUILD_ROOT)/.pgrx/data-15 && \
	if [ ! -f "$(YB_BUILD_ROOT)/.pgrx/config.toml" ] || ! grep -q "^pg15\s*=\s*\"$(PG_CONFIG)\"" "$(YB_BUILD_ROOT)/.pgrx/config.toml" >/dev/null 2>&1; then \
		$(YB_PGRX_RUN_ENV) \
		ASAN_OPTIONS=detect_leaks=0 \
		$(YB_CARGO_PGRX) pgrx init --pg15 $(PG_CONFIG); \
	fi

# YB: Consumers depend on install-pgrx purely for ordering; there is nothing
# to install into the PostgreSQL tree.
install: cargo-pgrx

clean: cargo-exists
	$(CARGO_BIN) clean
