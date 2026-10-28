# cargo pgrx regress

Runs SQL regression tests using expected-output diffing, similar to
Postgres' own `pg_regress` tool.

Regression tests execute SQL scripts against a live Postgres instance and
compare the output against committed expected-output files. This catches
changes in SQL-level behavior.

## What it does

1. Compiles and installs the extension
2. Starts a pgrx-managed Postgres instance
3. Creates (or reuses) a test database
4. For each test, runs `sql/<test>.sql` and compares output against
   `expected/<test>.out`
5. Reports diffs for any mismatches

## Usage

```
cargo pgrx regress [OPTIONS] [ARGS]...
```

### Arguments

Positional: `[pgXX] [testname]`

- `cargo pgrx regress` -- all tests, default pg version
- `cargo pgrx regress pg18` -- all tests against pg18
- `cargo pgrx regress pg18 mytest` -- specific test against pg18
- `cargo pgrx regress mytest` -- specific test, default pg version

### Key flags

| Flag | Short | Description |
|------|-------|-------------|
| `--auto` | `-a` | Overwrite expected output with actual output for failed tests |
| `--add <TEST>` | | Bootstrap a new test: run it, promote output to expected/, exit. Implies `--resetdb` |
| `--dry-run` | | Print what would happen without doing it |
| `--resetdb` | | Drop and recreate the test database before running |
| `--repeat <N>` | | Run the suite N times (default: 1). Useful for detecting flaky tests |
| `--dbname <DB>` | | Custom database name instead of auto-generated `<ext>_regress` |
| `--release` | `-r` | Compile in release mode |
| `--profile <P>` | | Specific Cargo profile |
| `--no-schema` | `-n` | Skip schema regeneration |
| `--runas <USER>` | | Run Postgres as this system user |
| `--pgdata <DIR>` | | Custom pgdata directory |
| `--psql-verbosity <V>` | | Error report verbosity: `default`, `verbose`, `terse`, `sqlstate` |
| `--postgresql-conf <K=V>` | | Custom postgresql.conf settings |
| `--features <F>` | `-F` | Cargo features |
| `--no-default-features` | | Disable default features |
| `--all-features` | | Enable all features |
| `--package <PKG>` | `-p` | Package in workspace |
| `--manifest-path <PATH>` | | Path to Cargo.toml |

## Examples

```bash
# Run all regression tests
cargo pgrx regress

# Run a specific test
cargo pgrx regress pg18 search_basic

# Create a new regression test (writes expected output)
cargo pgrx regress --add my_new_test

# See what changed without promoting anything
cargo pgrx regress

# After reviewing diffs, promote actual output to expected
cargo pgrx regress --auto

# Recreate the database (useful after schema changes)
cargo pgrx regress --resetdb

# Detect flaky tests by repeating
cargo pgrx regress --repeat 5

# Dry run to see which tests would execute
cargo pgrx regress --dry-run
```

## Workflow discipline

1. **Always run without `--auto` first.** See the diffs. Understand them.
2. **Only use `--auto` after confirming diffs are intentional.** `--auto`
   silently blesses whatever output Postgres produces. If the output is
   wrong, you have committed a bug as "expected."
3. **Use `--add` for new tests.** It runs the test's `setup.sql`, executes
   the test, and promotes the output in one step.
4. **Use `--resetdb` when schema changes.** Stale schema in the test database
   causes false failures.
5. **Do not hand-edit files in `expected/`.** Let `--auto` or `--add` write
   them, then review the git diff.

## When to use

- Validating SQL-level behavior of the extension
- Catching regressions in query output, error messages, or plan shapes
- Documenting expected behavior as executable tests

## When NOT to use

- Unit testing Rust logic -- use `#[test]`
- Testing Postgres internals from Rust -- use `#[pg_test]`
- Performance testing -- use `cargo pgrx bench`
