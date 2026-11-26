# cargo pgrx bench

Runs in-process Postgres benchmarks defined with `#[pg_bench]` functions.

Benchmarks execute inside a live Postgres backend with the extension loaded,
measuring real SQL and index operations with statistical rigor.

## What it does

1. Compiles and installs the extension (release mode by default)
2. Starts a pgrx-managed Postgres instance
3. Creates (or reuses) a benchmark database
4. Discovers and runs `#[pg_bench]` functions
5. Collects timing samples, computes statistics
6. Optionally compares against a previous named run

## Usage

```
cargo pgrx bench [OPTIONS] [ARGS]...
```

### Arguments

Positional: `[pgXX] [benchname]`

### Key flags

| Flag | Description |
|------|-------------|
| `--group-name <NAME>` | Tag this benchmark run with a name (for later comparison) |
| `--compare-group <NAME>` | Compare results against a previously tagged run |
| `--resetdb` | Recreate the benchmark database before running |
| `--cascade` | Use `CASCADE` when dropping the extension during refresh |
| `--list` | List discovered benchmark functions and exit |
| `--report` | Render a history report from the benchmark database |
| `--json` | Emit the summary as JSON |
| `--wait <SECS>` | Sleep N seconds after printing PID before starting (for attaching profilers) |
| `--debug` | Compile in debug mode (default is release) |
| `--profile <P>` | Specific Cargo profile |
| `--postgresql-conf <K=V>` | Custom postgresql.conf settings |
| `--dbname <DB>` | Custom database name |
| `--features <F>` | `-F` Cargo features |
| `--package <PKG>` | `-p` Package in workspace |
| `--manifest-path <PATH>` | Path to Cargo.toml |
| `--target <TARGET>` | Cross-compilation target |

## Examples

```bash
# Run all benchmarks
cargo pgrx bench

# Run a specific benchmark
cargo pgrx bench pg18 index_build

# Tag a baseline run
cargo pgrx bench --group-name before-optimization

# Run again and compare
cargo pgrx bench --group-name after-optimization --compare-group before-optimization

# List available benchmarks
cargo pgrx bench --list

# View historical results
cargo pgrx bench --report

# JSON output for CI integration
cargo pgrx bench --json

# Wait 5 seconds for profiler attachment
cargo pgrx bench --wait 5

# Fresh database for clean measurement
cargo pgrx bench --resetdb
```

## When to use

- Measuring performance of extension operations (index builds, queries, scans)
- Before/after comparison for optimization work
- CI performance regression detection

## When NOT to use

- Correctness testing -- use `cargo pgrx test` or `cargo pgrx regress`
- Compile-time checks -- use `cargo check`
- Pure Rust microbenchmarks with no Postgres dependency -- use `criterion`
