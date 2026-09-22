---
name: cargo-pgrx
description: "Choose and run cargo-pgrx commands, pgrx tests, pg_test coverage, and pg_sys boundary checks."
user-invocable: false
---

# cargo pgrx

You understand that a pgrx extension lives in two worlds. The extension's
shared library runs inside the Postgres backend -- it shares an address space
with the server, has access to every internal symbol, and obeys Postgres
memory management. The Rust test binary runs outside Postgres -- it is a
normal executable with no access to Postgres symbols whatsoever. Every
decision about testing, building, and running flows from knowing which world
you are in.

Your professional value is **never writing code that crosses the boundary
wrong**. You know which symbols live in Postgres and which live in Rust. You
know which `cargo` command produces which artifact. You do not guess.

## The two worlds

A pgrx extension compiles to a `cdylib` -- a shared library that Postgres
loads with `LOAD` or `CREATE EXTENSION`. At load time, the dynamic linker
resolves every `pg_sys::*` symbol against the running Postgres binary. This
works because the extension runs *inside* the server process.

A `#[test]` function compiles into a standalone test binary. This binary is
not Postgres. It does not link against Postgres. The `pg_sys::*` symbols do
not exist in its address space. Any reference to them causes linker errors
on both Linux and macOS.

## Test classification

Every test function belongs to exactly one category. There is no grey area.

**`#[test]`** -- pure Rust logic. Zero Postgres contact.

The function, and everything it transitively calls, must resolve without
Postgres. This means no `pg_sys::*` functions, no `pg_sys::*` statics,
and no pgrx wrappers that call into Postgres internally.

```rust
#[test]
fn oid_round_trips_through_bytes() {
    let oid = pg_sys::Oid::from(42);
    let bytes = oid.to_le_bytes();
    assert_eq!(u32::from_le_bytes(bytes), 42);
}
```

**`#[pg_test]`** -- needs Postgres. Runs inside a live backend.

`cargo pgrx test` starts a Postgres instance, installs the extension, and
executes `#[pg_test]` functions inside the server process where `pg_sys::*`
symbols are available.

```rust
#[pg_test]
fn spi_returns_value() {
    let val = Spi::get_one::<i64>("SELECT 42").unwrap();
    assert_eq!(val, Some(42));
}
```

### The classification rule

Ask one question: **does this function, or anything it calls, need a symbol
that lives in Postgres?**

If yes: `#[pg_test]`.
If no: `#[test]`.
If unsure: `#[pg_test]`. The cost of running a pure-Rust test inside Postgres
is a few milliseconds of overhead. The cost of running a Postgres-dependent
test outside Postgres is a linker error or a segfault.

### What counts as "needs Postgres"

All of these require Postgres and therefore require `#[pg_test]`:

| Category | Examples |
|----------|----------|
| Direct pg_sys calls | `pg_sys::palloc`, `pg_sys::elog`, `pg_sys::GetCurrentTransactionId` |
| pg_sys statics | `pg_sys::DataDir`, `pg_sys::CurrentMemoryContext`, `pg_sys::MyDatabaseId` |
| SPI | `Spi::get_one`, `Spi::connect`, `Spi::run` |
| Memory contexts | `PgMemoryContexts::*`, `palloc!`, `pfree!` |
| Relations | `PgRelation::open`, `PgRelation::with_lock` |
| Heap tuples | `PgHeapTuple::*` |
| Error reporting | `ereport!`, `pgrx::error!`, `pgrx::warning!` |
| PgBox | `PgBox::from_pg`, `PgBox::alloc` |
| GUC access | `GucSetting::get` at runtime |
| Any pgrx type with a `Drop` that calls pg_sys | `PgRelation`, `PgTupleDesc`, `SpiClient` |

Things that are safe in `#[test]`:

| Category | Examples |
|----------|----------|
| Pure types and enums | `pg_sys::Oid`, `pg_sys::Datum`, `pg_sys::BuiltinOid` |
| Constants | `pg_sys::BLCKSZ`, `pg_sys::InvalidOid` |
| Struct definitions | `pg_sys::HeapTupleData` (the *type*, not a live instance) |
| Derive macros | `#[derive(PostgresType)]` at compile time |
| Your own pure-Rust code | Parsers, data structures, serialization, algorithms |

## Command routing

Pick the narrowest command that achieves the goal:

| Intent | Command |
|--------|---------|
| Does it compile? | `cargo check` (not `cargo pgrx` -- plain cargo is fine and faster) |
| Run all tests | `cargo pgrx test` or `cargo test` |
| Run one test | `cargo pgrx test pg18 test_name` or `cargo test test_name` |
| Interactive REPL | `cargo pgrx run` |
| Install only (no psql) | `cargo pgrx run --install-only` or `cargo pgrx install` |
| SQL regression tests | `cargo pgrx regress` |
| Bootstrap new regression test | `cargo pgrx regress --add test_name` |
| Promote regression output | `cargo pgrx regress --auto` (review diffs first!) |
| Run benchmarks | `cargo pgrx bench` |
| Generate SQL schema | `cargo pgrx schema` |
| Create extension package | `cargo pgrx package` |

**`cargo check` is always valid.** It does not link, so the pg_sys boundary
is irrelevant. Use it freely for compile verification, IDE support, and
iterative development. It is faster than any `cargo pgrx` command.

**`cargo test` works**, and so does `cargo pgrx test`. The pgrx test framework
handles `#[pg_test]` execution correctly either way. The *only* problem is
putting pg_sys/pgrx/Postgres symbols inside `#[test]` functions -- that
causes linker errors (Linux) or silent crashes (macOS) because the test
binary is not linked against Postgres.

**`cargo build` is rarely what you want.** It builds the cdylib but does not
install it into Postgres. Use `cargo pgrx run` or `cargo pgrx install` to
get a working extension.

### The pgXX version argument

Most commands accept an optional Postgres version: `pg13`, `pg14`, `pg15`,
`pg16`, `pg17`, `pg18`, or `all`. If omitted, the default is determined by
the first `pgXX` feature in the crate's `Cargo.toml`. You rarely need to
specify it explicitly.

## Anti-patterns

**Putting pg_sys symbols in `#[test]` functions.** This is the single most
common mistake. `#[test]` functions run in a plain Rust binary with no
Postgres symbols available. Use `#[pg_test]` for anything that touches
Postgres.

**Using `--auto` without reviewing diffs.** `cargo pgrx regress --auto`
silently promotes actual output to expected output. If the output is *wrong*,
you have just blessed a bug. Always run without `--auto` first, review the
diffs, then promote.

**Running `cargo pgrx run` when you just need `cargo check`.** `cargo pgrx
run` compiles, generates schema, installs the extension, starts Postgres, and
opens psql. That is heavy. If you just want to know whether the code compiles,
`cargo check` finishes in seconds.

**Forgetting `--resetdb` on `cargo pgrx regress`.** If the extension schema
has changed, regression tests may fail because the test database has stale
schema. Use `--resetdb` to start fresh.

## Reference files

Each subcommand has a dedicated reference with full flags, examples, and
use-case guidance:

- [test.md](test.md) -- `cargo pgrx test`: run `#[test]` and `#[pg_test]` functions
- [run.md](run.md) -- `cargo pgrx run`: build, install, open psql
- [regress.md](regress.md) -- `cargo pgrx regress`: SQL regression tests
- [bench.md](bench.md) -- `cargo pgrx bench`: in-process benchmarks
- [install.md](install.md) -- `cargo pgrx install`: install into Postgres
- [schema.md](schema.md) -- `cargo pgrx schema`: generate SQL schema
- [new.md](new.md) -- `cargo pgrx new`: scaffold a new extension
- [init.md](init.md) -- `cargo pgrx init`: set up development environment
- [package.md](package.md) -- `cargo pgrx package`: create install package
- [instance-management.md](instance-management.md) -- `start`, `stop`, `status`, `connect`
- [utilities.md](utilities.md) -- `info`, `get`, `upgrade`, `cross`
