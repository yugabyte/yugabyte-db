# cargo pgrx install

Compiles the extension and installs it into a Postgres instance specified by
`pg_config`. Does not start Postgres or open a shell.

## What it does

1. Runs `cargo build --lib` to produce the shared library
2. Copies the `.so`/`.dylib` to Postgres' `pkglibdir`
3. Copies SQL files and the `.control` file to `sharedir`

## Usage

```
cargo pgrx install [OPTIONS]
```

### Key flags

| Flag | Short | Description |
|------|-------|-------------|
| `--pg-config <PATH>` | `-c` | Path to `pg_config` (default: first in `$PATH`) |
| `--release` | `-r` | Compile in release mode |
| `--profile <P>` | | Specific Cargo profile |
| `--test` | | Build in test mode (used internally by `cargo pgrx test`) |
| `--sudo` | `-s` | Use `sudo` for file installation |
| `--features <F>` | `-F` | Cargo features |
| `--no-default-features` | | Disable default features |
| `--all-features` | | Enable all features |
| `--package <PKG>` | `-p` | Package in workspace |
| `--manifest-path <PATH>` | | Path to Cargo.toml |
| `--target <TARGET>` | | Cross-compilation target |

## Examples

```bash
# Install to default pg_config
cargo pgrx install

# Install to a specific Postgres installation
cargo pgrx install --pg-config /usr/local/pgsql/bin/pg_config

# Release build for production
cargo pgrx install --release

# Install when extension dir requires root
cargo pgrx install --release --sudo
```

## When to use

- Installing into a non-pgrx-managed Postgres (production, staging)
- CI/CD deployment pipelines
- When you need `cargo pgrx run` behavior without the psql session

## When NOT to use

- Interactive development -- use `cargo pgrx run` instead
- Testing -- use `cargo pgrx test` instead
