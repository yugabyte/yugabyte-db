# Utility commands

## cargo pgrx info

Provides information about the pgrx-managed development environment.

### Subcommands

```bash
cargo pgrx info path pg18        # print Postgres install path
cargo pgrx info pg-config pg18   # print path to pg_config
cargo pgrx info version pg18     # print exact Postgres version string
```

## cargo pgrx get <property>

Reads a property from the extension's `.control` file.

```bash
cargo pgrx get comment           # print the extension comment
cargo pgrx get default_version   # print the default version
cargo pgrx get superuser         # print superuser requirement
```

Flags: `--package`, `--manifest-path`

## cargo pgrx upgrade

Upgrades pgrx crate versions in `Cargo.toml`.

```bash
# Upgrade to latest release
cargo pgrx upgrade

# Upgrade to specific version
cargo pgrx upgrade --to 0.17.0

# Preview changes without modifying Cargo.toml
cargo pgrx upgrade --dry-run

# Include pre-release versions
cargo pgrx upgrade --include-prereleases

# Upgrade a specific workspace member
cargo pgrx upgrade --package my-extension
```

Flags: `--to <VERSION>`, `--manifest-path`, `--dry-run`,
`--include-prereleases`, `--package`

## cargo pgrx cross (experimental)

Commands for cross-compilation support.

```bash
cargo pgrx cross             # see available cross subcommands
```

This is experimental and not commonly used in normal development workflows.
