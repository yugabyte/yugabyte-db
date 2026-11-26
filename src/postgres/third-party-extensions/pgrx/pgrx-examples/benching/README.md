# benching

This example demonstrates the new in-process `#[pg_bench]` workflow.

It includes:

- a simple pure-Rust benchmark that exercises a string transform
- a mutating SPI benchmark that uses `setup = prepare_spi_fixture` and `transaction = "subtransaction_per_batch"`

## Layout

Benchmarks live under:

```rust
#[cfg(feature = "pg_bench")]
#[pg_schema]
mod benches { ... }
```

The `pg_bench` feature is intentionally separate so benchmark wrappers and helper
dependencies are not shipped in a normal extension build.

## Running

From this directory:

```bash
cargo pgrx bench
```

Or choose an explicit Postgres version and group name:

```bash
cargo pgrx bench pg16 --group-name initial-run
```

To compare against a named prior group:

```bash
cargo pgrx bench --compare-group initial-run
```

To print the backend PID and leave time to attach a profiler or debugger:

```bash
cargo pgrx bench --wait 10
```

To inspect the SQL-visible wrappers without running them:

```bash
cargo pgrx bench --list
```
