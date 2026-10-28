# cargo pgrx package

Creates an installation package directory containing the compiled extension
and all files needed to install it into a Postgres instance.

Used for distribution and deployment, not development.

## What it does

1. Compiles the extension (release mode by default)
2. Creates a directory tree mirroring the Postgres installation layout
3. Copies the shared library, SQL files, and control file into the tree

## Usage

```
cargo pgrx package [OPTIONS]
```

### Key flags

| Flag | Short | Description |
|------|-------|-------------|
| `--pg-config <PATH>` | `-c` | Target `pg_config` (determines install paths) |
| `--out-dir <DIR>` | | Output directory (default: `target/<profile>/<ext>-pgXX/`) |
| `--debug` | `-d` | Build in debug mode (default is release) |
| `--profile <P>` | | Specific Cargo profile |
| `--test` | | Build in test mode |
| `--features <F>` | `-F` | Cargo features |
| `--package <PKG>` | `-p` | Package in workspace |
| `--manifest-path <PATH>` | | Path to Cargo.toml |
| `--target <TARGET>` | | Cross-compilation target |

## Examples

```bash
# Create a package for the default Postgres
cargo pgrx package

# Package for a specific Postgres installation
cargo pgrx package --pg-config /usr/local/pgsql/bin/pg_config

# Custom output directory
cargo pgrx package --out-dir ./dist

# Package for cross-compilation target
cargo pgrx package --target x86_64-unknown-linux-gnu
```

## When to use

- Building release artifacts for deployment
- Creating packages for package managers (deb, rpm, etc.)
- CI pipelines that produce installable artifacts
