# cargo pgrx init

One-time initialization of the pgrx development environment. Downloads,
compiles, and configures Postgres versions that pgrx will manage.

Run this once after installing pgrx, or when adding support for a new
Postgres version.

## What it does

1. For each specified Postgres version: downloads source, compiles, and
   installs into `~/.pgrx/`
2. Configures data directories and ports for each version
3. Records paths in `~/.pgrx/config.toml`

## Usage

```
cargo pgrx init [OPTIONS]
```

### Flags

Each Postgres version has its own flag. The value is either:
- A path to an existing `pg_config` binary
- The string `download` to have pgrx download and compile it

| Flag | Env var | Description |
|------|---------|-------------|
| `--pg13 <PATH\|download>` | `PG13_PG_CONFIG` | Postgres 13 |
| `--pg14 <PATH\|download>` | `PG14_PG_CONFIG` | Postgres 14 |
| `--pg15 <PATH\|download>` | `PG15_PG_CONFIG` | Postgres 15 |
| `--pg16 <PATH\|download>` | `PG16_PG_CONFIG` | Postgres 16 |
| `--pg17 <PATH\|download>` | `PG17_PG_CONFIG` | Postgres 17 |
| `--pg18 <PATH\|download>` | `PG18_PG_CONFIG` | Postgres 18 |
| `--base-port <PORT>` | | Base port number for managed instances |
| `--base-testing-port <PORT>` | | Base port for test instances |
| `--configure-flag <FLAG>` | | Extra flags for Postgres' `./configure` |
| `--no-run` | | Don't run compiled binaries (for cross-compilation) |
| `--valgrind` | | Compile Postgres with Valgrind instrumentation |
| `-j, --jobs <N>` | | Parallel make jobs |

## Examples

```bash
# Download and compile Postgres 18
cargo pgrx init --pg18=download

# Use an already-installed Postgres
cargo pgrx init --pg18=/usr/local/pgsql/bin/pg_config

# Initialize multiple versions
cargo pgrx init --pg16=download --pg17=download --pg18=download

# Use system Postgres for one version, download another
cargo pgrx init --pg17=/usr/bin/pg_config --pg18=download

# Parallel compilation
cargo pgrx init --pg18=download -j8
```

## When to use

- First-time pgrx setup on a new machine
- Adding a new Postgres version to the development environment
- After upgrading pgrx (if managed Postgres versions need rebuilding)
