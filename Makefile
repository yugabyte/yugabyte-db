.PHONY: cargo-exists cargo-pgrx-exists pg_parquet install clean check uninstall

PG_CONFIG ?= pg_config

all: pg_parquet

cargo-exists:
	   @which cargo > /dev/null || (echo "cargo is not available. Please install cargo." && exit 1)

cargo-pgrx-exists: cargo-exists
	   @which cargo-pgrx > /dev/null || (echo "cargo-pgrx is not available. Please install cargo-pgrx." && exit 1)

pg_parquet: cargo-pgrx-exists
	   cargo build --release --features pg17

install: pg_parquet
	   cargo pgrx install --release --features pg17

clean: cargo-exists
	   cargo clean

check: cargo-pgrx-exists
	   RUST_TEST_THREADS=1 cargo pgrx test pg17

uninstall:
	   rm -f $(shell $(PG_CONFIG) --pkglibdir)/pg_parquet.so
	   rm -f $(shell $(PG_CONFIG) --sharedir)/extension/pg_parquet*
