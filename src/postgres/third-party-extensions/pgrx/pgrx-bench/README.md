# pgrx-bench

`pgrx-bench` is the runtime support crate behind `#[pg_bench]` benchmarks in `pgrx`
extensions.

You'll generally use it together with `cargo pgrx bench` during extension development. It
exists to make in-process benchmarking of extension code feel natural from Rust while still
respecting the way Postgres extensions are built, installed, loaded, and exercised.

It is not a standalone benchmark runner. The command-line workflow lives in
[`cargo-pgrx`](../cargo-pgrx/README.md). This crate provides the author-facing runtime API
that benchmark functions use once they are executing inside Postgres.

## What This Is

`pgrx-bench` is intended for **in-process** benchmarks of extension code.

That means:

- the benchmark function itself runs inside a Postgres backend
- the timed code can call normal Rust functions, `Spi`, and other Postgres internals directly
- benchmark definitions live next to the rest of your extension code
- `cargo pgrx bench` installs a bench-enabled build of your extension into a managed benchmark
  database and records the results there

This makes it a good fit for questions like:

- "What is the cost of this Rust helper when called from an extension?"
- "How expensive is this SPI-heavy path?"
- "Did this change make the extension function faster or slower compared to the previous run?"
- "How does this benchmark behave across repeated runs on the same managed Postgres instance?"

## What This Is Not

`pgrx-bench` is **not**:

- a client-side SQL driver benchmark tool
- a load-testing or concurrency-testing framework
- a replacement for regression tests or `#[pg_test]`
- a tool for benchmarking top-level transaction commit cost

The important design point is that benchmarking happens inside the backend, not by repeatedly
issuing SQL over a client connection and timing that from the outside.

If what you need is correctness testing, use `cargo pgrx test` or `cargo pgrx regress`.
If what you need is in-process performance measurement of extension code, use `cargo pgrx bench`.

## When To Use It

Reach for `#[pg_bench]` when:

- you want stable, repeatable microbenchmarks or small mesobenchmarks for extension code
- the code under test is meaningful only inside Postgres
- you want results stored in a database and compared against earlier benchmark runs
- you want to exercise SPI or other backend services directly from the timed code

It is especially useful while iterating on:

- expression evaluation
- parser/planner support logic
- tuple conversion and datum conversion paths
- SPI-heavy data manipulation
- indexing or operator support functions

## How It Fits With `cargo-pgrx`

`pgrx-bench` is intentionally paired with `cargo pgrx bench`.

`cargo pgrx bench` does the operational work:

- enables the `pg_bench` Cargo feature for the build
- builds the extension in `--release` mode by default
- installs the extension artifacts using the normal `cargo pgrx install` path
- refreshes the extension in a managed database named `$extname_benches`
- discovers benchmark wrappers
- executes each benchmark in-process
- rolls back benchmark side effects after each benchmark invocation
- persists normalized results, environment metadata, and settings into a runner-owned schema

`pgrx-bench` handles the runtime side inside the backend:

- `Bencher`
- `BatchSize`
- `black_box`
- transaction-aware execution support
- serialization of benchmark results back to the CLI runner

Most users should think of this crate as "the runtime API used by `#[pg_bench]`".

## Adding Benchmarks To An Extension

Benchmarks are gated behind a dedicated Cargo feature so they are not included in a normal
extension build unless you explicitly ask for them.

Typical Cargo.toml shape:

```toml
[features]
pg16 = ["pgrx/pg16", "pgrx-tests/pg16"]
pg_test = []
pg_bench = ["dep:pgrx-bench"]

[dependencies]
pgrx = "=0.17.0"
pgrx-bench = { version = "=0.17.0", optional = true }

[dev-dependencies]
pgrx-tests = "=0.17.0"
```

Notice that `pgrx-bench` does not need its own `pg16`/`pg17`/etc. passthrough feature wiring.
The active Postgres version is already selected on `pgrx`, and `pgrx-bench` itself stays free of
`pgrx-*` dependencies and Postgres-version feature flags. The proc-macro-generated wrapper code in
the extension crate owns the Postgres-specific boundary work instead.

Benchmark functions live under a feature-gated `#[pg_schema] mod benches`.

```rust
use pgrx::prelude::*;

::pgrx::pg_module_magic!(name, version);

#[pg_extern]
fn normalize_phrase(input: &str) -> String {
    input
        .split_whitespace()
        .map(|word| word.trim_matches(|ch: char| !ch.is_alphanumeric()).to_ascii_lowercase())
        .filter(|word| !word.is_empty())
        .collect::<Vec<_>>()
        .join(" ")
}

#[cfg(feature = "pg_bench")]
#[pg_schema]
mod benches {
    use pgrx::prelude::*;
    use pgrx_bench::{BatchSize, Bencher, black_box};

    fn prepare_spi_fixture() {
        Spi::run(
            "CREATE UNLOGGED TABLE IF NOT EXISTS bench_sink (
                value integer NOT NULL
            )",
        )
        .unwrap();
        Spi::run("TRUNCATE bench_sink").unwrap();
    }

    #[pg_bench]
    fn bench_normalize_phrase(b: &mut Bencher) {
        let input = "The QUICK, Brown fox jumped over the lazy dog";
        b.iter(|| black_box(crate::normalize_phrase(black_box(input))));
    }

    #[pg_bench(
        setup = prepare_spi_fixture,
        transaction = "subtransaction_per_batch",
        sample_size = 50,
        measurement_time_ms = 2_000
    )]
    fn bench_spi_insert_batch(b: &mut Bencher) {
        b.iter_batched(
            || (0..32).collect::<Vec<i32>>(),
            |values| {
                for value in values {
                    Spi::run(&format!("INSERT INTO bench_sink VALUES ({value})")).unwrap();
                }
            },
            BatchSize::SmallInput,
        );
    }
}
```

## Benchmark Function Shape

A `#[pg_bench]` function must accept `&mut pgrx_bench::Bencher` and must register exactly one
timing loop using either:

- `b.iter(...)`
- `b.iter_batched(...)`

The crate exports:

- `Bencher`
- `BatchSize`
- `black_box`

The goal is to feel familiar if you have used Criterion before, while still giving `pgrx` control
over transaction handling and result capture.

## `black_box`

`pgrx-bench` re-exports Criterion's `black_box` directly:

```rust
use pgrx_bench::black_box;
```

Its job is to make a value opaque to the optimizer so the compiler is less able to constant-fold,
inline away, or otherwise "solve" part of the benchmark ahead of time.

That matters because microbenchmarks are especially vulnerable to unrealistic optimization. If the
compiler can prove that an input is always the same, or that the result is irrelevant, it may
remove work that you actually meant to measure.

Typical usage looks like this:

```rust
#[pg_bench]
fn bench_normalize_phrase(b: &mut Bencher) {
    let input = "The QUICK, Brown fox jumped over the lazy dog";
    b.iter(|| black_box(crate::normalize_phrase(black_box(input))));
}
```

Here there are two useful boundaries:

- `black_box(input)` makes the benchmark input less predictable to the optimizer
- `black_box(...)` around the result makes it less likely that the whole call can be discarded as
  unused

In practice, the input side is the one you should think about first. If your benchmark uses
literals, precomputed fixtures, or values that are obviously constant, wrapping those inputs is
often the difference between benchmarking your code and benchmarking what the optimizer managed to
precompute.

### Why use it if Criterion already does?

Criterion itself already uses `black_box` internally in a number of timing loops, especially around
routine outputs. That helps, but it is not a substitute for author intent at the benchmark boundary.

You should still use `black_box` in your benchmark code when:

- the function input is a constant or easy-to-reason-about value
- the computation could be folded because the compiler can "see through" the closure
- you want to make it explicit which values belong to the code under test

### Relationship to Criterion's version

This is not a `pgrx`-specific implementation. `pgrx-bench` re-exports Criterion's own helper so
the behavior tracks the Criterion version bundled with `pgrx-bench`.

In the current Criterion version used here, `black_box`:

- uses `test::black_box` when Criterion is built with its `real_blackbox` feature
- otherwise falls back to a stable-compatible implementation based on volatile reads

That fallback is useful and widely used, but it is not magic. It can add some overhead, and it may
not block every possible optimization on every compiler and platform. The goal is to make the
benchmark substantially more realistic, not to guarantee perfect optimizer isolation.

### A few rules of thumb

- Use `black_box` around benchmark inputs when those inputs are compile-time constants or reused
  fixtures.
- Usually keep `black_box` close to the code under test, not wrapped around an entire setup phase.
- Do not use `black_box` as a substitute for good benchmark structure. Setup still belongs in
  `setup = ...` or `iter_batched(...)`, not inside the timed code unless that setup is part of what
  you intentionally want to measure.
- If a benchmark becomes materially different when you remove `black_box`, the version with
  `black_box` is usually the more trustworthy one.

## Setup Functions

Benchmarks may optionally declare a one-time setup function:

```rust
#[pg_bench(setup = prepare_spi_fixture)]
fn bench_spi_insert_batch(b: &mut Bencher) {
    // ...
}
```

Setup functions:

- are plain Rust functions
- take no arguments
- run once per benchmark invocation
- run before warmup and before measured samples
- are never included in the timed portion of the benchmark

Use a setup function for benchmark-level fixture preparation. If you need fresh per-iteration or
per-batch input, that belongs in `iter_batched(...)`.

## Transaction Semantics

Transaction handling is one of the main reasons `pgrx-bench` exists as a dedicated runtime.

Supported transaction modes are:

- `shared`
- `subtransaction_per_batch`
- `subtransaction_per_iteration`

### `shared`

This is the default.

The benchmark invocation shares one outer transaction for setup, warmup, and measured execution.
It has the lowest overhead and is often the right choice for pure Rust or read-heavy benchmarks.

### `subtransaction_per_batch`

Each measured batch runs inside an internal Postgres subtransaction. This is usually the best
choice for mutating SPI benchmarks because it limits state leakage within the benchmark while
keeping overhead manageable.

### `subtransaction_per_iteration`

Each individual iteration runs inside its own internal subtransaction. This offers the cleanest
isolation, but also the highest overhead.

### Outer rollback behavior

Regardless of the transaction mode chosen for the timed loop, `cargo pgrx bench` rolls back the
benchmark invocation after collecting its result payload. This means benchmark fixture changes do
not accumulate in the persistent benchmark database.

The benchmark history remains durable because the CLI persists the result rows in a separate
runner-owned transaction after the benchmark transaction has been rolled back.

## Running Benchmarks

From your extension directory:

```console
$ cargo pgrx bench
```

Or with an explicit Postgres version:

```console
$ cargo pgrx bench pg16
```

To run a single benchmark:

```console
$ cargo pgrx bench pg16 bench_normalize_phrase
```

To list discovered benchmarks and their settings:

```console
$ cargo pgrx bench --list
bench_normalize_phrase [transaction=shared, setup=none, sample_size=100, warm_up=3000ms, measurement=5000ms, nresamples=100000, noise_threshold=0.01, significance_level=0.05]
bench_spi_insert_batch [transaction=subtransaction_per_batch, setup=prepare_spi_fixture, sample_size=50, warm_up=3000ms, measurement=2000ms, nresamples=100000, noise_threshold=0.01, significance_level=0.05]
```

During execution, the CLI prints which benchmark is currently running and the effective settings
for that benchmark.

## Result Storage

Benchmark results are stored in a managed database named `$extname_benches` by default.

Inside that database:

- your extension is installed and refreshed for each bench run
- historical benchmark data is stored in a runner-owned schema named `pgrx_bench`
- results are grouped by benchmark invocation
- environment metadata and `pg_settings` snapshots are recorded alongside the results

This gives you a durable history of runs while still allowing SQL-visible changes in the extension
to be picked up by a `DROP EXTENSION` / `CREATE EXTENSION` refresh cycle.

## Comparison Groups

Each `cargo pgrx bench` invocation creates a named run group.

By default:

- the group name is auto-generated
- the comparison target is the most recent prior completed group

You can override either side explicitly:

```console
$ cargo pgrx bench --group-name before-rewrite
$ cargo pgrx bench --group-name after-rewrite --compare-group before-rewrite
```

The chosen comparison target is stored with the run group so later SQL analysis and CLI summaries
use the same baseline.

## What Gets Queried Later

The benchmark runner creates SQL tables and views for historical analysis. In particular, the
`pgrx_bench` schema is intended to make it easy to answer questions like:

- "What did this benchmark do on the last run?"
- "What changed relative to a named baseline?"
- "What were the Postgres settings during that run?"
- "Which group was this run compared against?"

`pgrx-bench` itself does not expose the SQL API. That lifecycle and persistence behavior belongs to
`cargo-pgrx`, but it is part of the overall feature this crate participates in.

## What To Expect In Normal Builds

Because benchmarks are feature-gated behind `pg_bench`, they are normally absent from:

- ordinary `cargo pgrx run`
- ordinary `cargo pgrx install`
- ordinary `cargo pgrx package`

If you explicitly enable `pg_bench` for those commands, `cargo-pgrx` will warn, but it will still
let you proceed. That can be useful for interactive exploration, but it is usually not what you
want for packaged releases.

## See Also

- [`cargo-pgrx`](../cargo-pgrx/README.md)
- [`pgrx-examples/benching`](../pgrx-examples/benching/README.md)
