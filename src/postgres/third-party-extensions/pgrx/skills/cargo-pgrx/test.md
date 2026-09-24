# cargo pgrx test

Runs both `#[test]` and `#[pg_test]` functions for a pgrx extension.

Both `cargo pgrx test` and plain `cargo test` work for pgrx extensions --
the pgrx test framework handles `#[pg_test]` execution either way.
`cargo pgrx test` adds convenience (auto-starts Postgres, manages features)
but is not strictly required.

## What it does

1. Determines the target Postgres version (from argument or Cargo.toml features)
2. Compiles the extension as a cdylib and as a test binary
3. Generates and installs the SQL schema into a pgrx-managed Postgres instance
4. Starts the Postgres instance if not already running
5. Runs `cargo test` with the `pg_test` feature enabled
6. `#[pg_test]` functions execute inside the Postgres backend process
7. `#[test]` functions execute in the test binary as usual

## Usage

```
cargo pgrx test [OPTIONS] [PG_VERSION] [TESTNAME]
```

### Arguments

| Argument | Description |
|----------|-------------|
| `PG_VERSION` | `pg13`, `pg14`, `pg15`, `pg16`, `pg17`, `pg18`, or `all`. Defaults to the first pgXX feature in Cargo.toml. Env: `PG_VERSION` |
| `TESTNAME` | If specified, only run tests whose names contain this string |

**Smart argument detection:** If the first positional argument is not a
recognized Postgres version (`pgXX` or `all`), it is treated as `TESTNAME`
and the default Postgres version is used. This means `cargo pgrx test foo`
works as a shorthand for `cargo pgrx test pgXX foo`.

### Flags

| Flag | Short | Description |
|------|-------|-------------|
| `--release` | `-r` | Compile in release mode (default is debug) |
| `--profile <P>` | | Use a specific Cargo profile (conflicts with `--release`) |
| `--no-schema` | `-n` | Skip SQL schema regeneration |
| `--runas <USER>` | | Use `sudo` to run the Postgres instance as this system user |
| `--pgdata <DIR>` | | Store the test database cluster in this directory |
| `--features <F>` | `-F` | Space-separated list of Cargo features to activate |
| `--no-default-features` | | Disable default features |
| `--all-features` | | Enable all features |
| `--package <PKG>` | `-p` | Select a specific package in a workspace |
| `--manifest-path <PATH>` | | Path to Cargo.toml |

## Examples

```bash
# Run all tests against the default Postgres version
cargo pgrx test

# Run all tests against Postgres 18
cargo pgrx test pg18

# Run only tests whose names contain "spi" (using default PG version)
cargo pgrx test spi

# Run only tests whose names contain "spi" against Postgres 18
cargo pgrx test pg18 spi

# Run tests in release mode
cargo pgrx test --release

# Run tests for a specific workspace member
cargo pgrx test --package my-extension

# Run tests against every configured Postgres version
cargo pgrx test all

# Skip schema regeneration (faster if schema hasn't changed)
cargo pgrx test --no-schema
```

## When to use

- **Always** when you want to run tests for a pgrx extension
- After modifying extension code to verify correctness
- In CI pipelines as the primary test command

## When NOT to use

- For compile-checking only -- use `cargo check` instead (faster)
- For SQL regression testing -- use `cargo pgrx regress` instead
- For benchmarking -- use `cargo pgrx bench` instead

## Common issues

**Linker errors mentioning Postgres symbols.** A `#[test]` function
(or code it calls) references `pg_sys` symbols. Change it to `#[pg_test]`.

**Tests hang or timeout.** The pgrx-managed Postgres instance may be in a
bad state. Try `cargo pgrx stop` then re-run.

**Schema mismatch errors.** The installed extension schema may be stale.
Remove `--no-schema` or explicitly reinstall.
