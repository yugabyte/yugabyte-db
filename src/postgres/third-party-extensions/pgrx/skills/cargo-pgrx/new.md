# cargo pgrx new

Scaffolds a new pgrx extension crate with a working project structure.

## What it creates

- `Cargo.toml` with pgrx dependencies and pgXX feature flags
- `src/lib.rs` with a minimal `#[pg_extern]` function
- `.cargo/config.toml` with macOS linker flags
- `<name>.control` extension control file
- SQL setup files
- `.gitignore`

## Usage

```
cargo pgrx new [OPTIONS] <NAME>
```

### Arguments

| Argument | Description |
|----------|-------------|
| `NAME` | The extension name (becomes the crate name and Postgres extension name) |

### Flags

| Flag | Short | Description |
|------|-------|-------------|
| `--bgworker` | `-b` | Generate a background worker template instead of the default |

## Examples

```bash
# Create a standard extension
cargo pgrx new my_extension

# Create a background worker extension
cargo pgrx new my_worker --bgworker
```

## After creation

```bash
cd my_extension
cargo pgrx run pg18     # build, install, and open psql
```

The scaffolded project is immediately runnable with `cargo pgrx run`.
