# YB: Shared setup (env vars, clang/gcc detection, cargo-pgrx bootstrap) lives in
# ../pgrx-common.mk.

.PHONY: cargo-exists cargo-pgrx-exists pg_parquet install clean check uninstall

PGRX_DIR := ../pgrx

include ../pgrx-common.mk

# YB: Keep the pre-flight cargo check for a friendlier error if cargo is absent.
cargo-pgrx-exists: cargo-exists

cargo-exists:
	$(CARGO_BIN) --version > /dev/null || (echo "cargo is not available. Please install cargo." && exit 1)

pg_parquet: cargo-pgrx-exists
	$(YB_PGRX_ENV) cargo build --release --features pg17

install: cargo-pgrx-exists
	# YB: Using local cargo-pgrx binary to install pg_parquet extension.
	export PATH="$(CARGO_DIR):$$PATH" && \
	$(YB_PGRX_ENV) \
		LD_LIBRARY_PATH="$(YB_THIRDPARTY_DIR)/installed/common/lib:$$LD_LIBRARY_PATH" \
		PGRX_HOME=$(YB_BUILD_ROOT)/.pgrx \
		$(PGRX_DIR)/target/release/cargo-pgrx pgrx install --release -F pg15 -c $(PG_CONFIG) && \
	[ "$$YB_PG_PARQUET_DEBUG_CLEAN" = "1" ] && rm -rf target/debug || true

clean: cargo-exists
	# YB: cleaning pgrx and pg_parquet.
	cd $(PGRX_DIR) && $(CARGO_BIN) clean && cd - && $(CARGO_BIN) clean

check: cargo-pgrx-exists
	# YB: Using local cargo-pgrx binary to run pg_parquet tests.
	export PATH="$(CARGO_DIR):$$PATH" && \
	RUST_TEST_THREADS=1 $(PGRX_DIR)/target/release/cargo-pgrx pgrx test pg15

uninstall:
	rm -f $(shell $(PG_CONFIG) --pkglibdir)/pg_parquet.so
	rm -f $(shell $(PG_CONFIG) --sharedir)/extension/pg_parquet*
