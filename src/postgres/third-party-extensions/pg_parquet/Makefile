.PHONY: all pg_parquet install clean check uninstall

# YB: Toolchain detection, cargo env scrubbing, and PG_CONFIG/CARGO_BIN live in
# the shared pgrx fragment. cargo-pgrx itself is built by the pgrx directory,
# ordered ahead of this directory by the parent Makefile. The $(YB_CARGO_PGRX) rule
# below bootstraps it for direct invocations only.

include ../pgrx/pgrx-common.mk

# YB: `all` stays the first target so it remains the default goal: the parent
# Makefile invokes this directory with no target, and a file rule first would
# make the build phase bootstrap cargo-pgrx.
all:

# YB: Direct invocations bootstrap cargo-pgrx themselves. Under the parent
# Makefile install-pgrx has already built it, so this file rule does not fire.
$(YB_CARGO_PGRX):
	   $(MAKE) -C $(YB_PGRX_DIR) cargo-pgrx

pg_parquet:
	   cargo build --release --features pg17

install: $(YB_CARGO_PGRX)
	   export PATH="$(CARGO_DIR):$$PATH" && \
	   $(YB_PGRX_CARGO_ENV) \
		   $(YB_PGRX_RUN_ENV) \
		   $(YB_CARGO_PGRX) pgrx install --release -F pg15 -c $(PG_CONFIG) && \
	   [ "$$YB_PG_PARQUET_DEBUG_CLEAN" = "1" ] && rm -rf target/debug || true

clean:
	   # YB: pgrx is cleaned by ../pgrx.
	   $(CARGO_BIN) clean

check: $(YB_CARGO_PGRX)
	   export PATH="$(CARGO_DIR):$$PATH" && \
	   RUST_TEST_THREADS=1 $(YB_CARGO_PGRX) pgrx test pg15

uninstall:
	   rm -f $(shell $(PG_CONFIG) --pkglibdir)/pg_parquet.so
	   rm -f $(shell $(PG_CONFIG) --sharedir)/extension/pg_parquet*
