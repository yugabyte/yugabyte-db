# cargo pgrx schema

Generates the extension's SQL schema from `#[pg_extern]`, `#[pg_operator]`,
and other pgrx annotations.

## What it does

1. Compiles the extension (builds a temporary binary that introspects the crate)
2. Extracts all SQL entity definitions from the compiled metadata
3. Produces a SQL file defining functions, types, operators, casts, etc.

## Usage

```
cargo pgrx schema [OPTIONS] [PG_VERSION]
```

### Key flags

| Flag | Short | Description |
|------|-------|-------------|
| `--out <PATH>` | `-o` | Write SQL to a file (default: stdout) |
| `--dot <PATH>` | `-d` | Write a GraphViz DOT dependency graph |
| `--skip-build` | | Reuse existing build artifacts |
| `--test` | | Build in test mode |
| `--pg-config <PATH>` | `-c` | Path to `pg_config` |
| `--release` | `-r` | Compile in release mode |
| `--profile <P>` | | Specific Cargo profile |
| `--features <F>` | `-F` | Cargo features |
| `--package <PKG>` | `-p` | Package in workspace |
| `--manifest-path <PATH>` | | Path to Cargo.toml |
| `--target <TARGET>` | | Cross-compilation target |

## Examples

```bash
# Print schema to stdout
cargo pgrx schema pg18

# Write schema to a file
cargo pgrx schema pg18 -o extension--1.0.sql

# Generate dependency graph
cargo pgrx schema pg18 --dot deps.dot

# Skip recompilation if nothing changed
cargo pgrx schema --skip-build
```

## When to use

- Inspecting the generated SQL for debugging
- Producing schema files for distribution
- Visualizing extension entity dependencies

## When NOT to use

- Schema is generated automatically by `cargo pgrx run`, `cargo pgrx test`,
  and `cargo pgrx install`. You rarely need to invoke this directly.
