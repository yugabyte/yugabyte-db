# cargo pgrx run

Compiles the extension, installs it into a pgrx-managed Postgres instance,
starts the instance, and opens an interactive `psql` session.

This is the primary development loop command: edit code, `cargo pgrx run`,
test interactively in psql, repeat.

## What it does

1. Compiles the extension (`cargo build --lib`)
2. Generates and installs the SQL schema
3. Starts the pgrx-managed Postgres instance (if not running)
4. Creates the target database if it does not exist
5. Opens `psql` (or `pgcli`) connected to the database

## Usage

```
cargo pgrx run [OPTIONS] [PG_VERSION] [DBNAME]
```

### Arguments

| Argument | Description |
|----------|-------------|
| `PG_VERSION` | `pg13`..`pg18`. Defaults to first pgXX feature in Cargo.toml. Env: `PG_VERSION` |
| `DBNAME` | Database to connect to (and create if needed). Defaults to the extension name |

**Smart argument detection:** If the first positional argument is not a
recognized Postgres version (`pgXX`), it is treated as `DBNAME` and the
default Postgres version is used. This means `cargo pgrx run mydb` works
as a shorthand for `cargo pgrx run pgXX mydb`.

### Flags

| Flag | Short | Description |
|------|-------|-------------|
| `--release` | `-r` | Compile in release mode |
| `--profile <P>` | | Specific Cargo profile |
| `--install-only` | | Install the extension but do not launch psql |
| `--pgcli` | | Use `pgcli` instead of `psql`. Env: `PGRX_PGCLI` |
| `--valgrind` | | Run Postgres under Valgrind |
| `--features <F>` | `-F` | Cargo features to activate |
| `--no-default-features` | | Disable default features |
| `--all-features` | | Enable all features |
| `--package <PKG>` | `-p` | Package in workspace |
| `--manifest-path <PATH>` | | Path to Cargo.toml |
| `--target <TARGET>` | | Cross-compilation target |

## Examples

```bash
# Build, install, and open psql against default Postgres
cargo pgrx run

# Connect to a specific database (using default PG version)
cargo pgrx run mydb

# Target a specific Postgres version and database
cargo pgrx run pg18 mydb

# Install only, don't open psql (useful for scripted workflows)
cargo pgrx run --install-only

# Use pgcli for a nicer interactive experience
cargo pgrx run --pgcli

# Release mode for performance testing
cargo pgrx run --release
```

## When to use

- Interactive development and manual testing
- Quick smoke tests after code changes
- Exploring extension behavior with ad-hoc SQL

## When NOT to use

- Running automated tests -- use `cargo pgrx test`
- Just checking compilation -- use `cargo check`
- Installing into a non-pgrx-managed Postgres -- use `cargo pgrx install`
