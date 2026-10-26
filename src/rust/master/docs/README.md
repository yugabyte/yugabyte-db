# The Rust master

The Rust master replaces the C++ master in `src/yb/master` one layer at a time on the
`feature-stack/hari-yb-master-rust` branch, and lands on main in one merge that also deletes the
C++ master. Until then the branch builds both: the C++ `yb-master` and the Rust `yb-master-rs`.

The sys-catalog tablet (Raft, WAL, DocDB) stays the shared C++ engine, hosted by
`src/yb/master_host/`. Rust owns the master logic above it.

| Doc | Covers |
|---|---|
| [cpp-host.md](cpp-host.md) | The master's C++ host and its bridges |
| [testing.md](testing.md) | Unit tests, conformance against the C++ master, itests |

Shared Rust pieces (build, bridges, protobuf) are documented in [src/rust/docs](../../docs/README.md).

## Crates

| Crate | Owns |
|---|---|
| `master` | The static library `yb-master-rs` links; `run_master` |
| `master-catalog-entity` | Sys-catalog rows by `SysRowEntryType` (layer 1: parse and re-serialize) |
| `tools/conformance` (`master-conformance`) | Checks Rust re-serialization of real rows against C++ |
