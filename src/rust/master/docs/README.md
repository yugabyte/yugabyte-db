# The Rust master

The Rust master replaces the C++ master in `src/yb/master` one layer at a time on the
`feature-stack/hari-yb-master-rust` branch, and lands on main in one merge that also deletes the
C++ master. Until then the branch builds both: the C++ `yb-master` and the Rust `yb-master-rs`.

The sys-catalog tablet (Raft, WAL, DocDB) stays the shared C++ engine, hosted by
`src/yb/master_host/`. Rust owns the master logic above it.

In layer 1 the Rust master is a follower only. It joins a quorum of C++ masters, on an existing
master's data or by remote bootstrap from shell mode, applies everything they replicate, and
answers the RPCs a C++ follower answers. It never leads: it does not start elections and steps
down if it wins one.

| Doc | Covers |
|---|---|
| [cpp-host.md](cpp-host.md) | The master's C++ host and its bridges |
| [web.md](web.md) | The web port and pages |
| [testing.md](testing.md) | Unit tests, conformance against the C++ master, itests |

Shared Rust pieces (build, bridges, protobuf, flags) are documented in
[src/rust/docs](../../docs/README.md).

## Crates

| Crate | Owns |
|---|---|
| `master` | The static library `yb-master-rs` links: flag registration, runtime, wiring |
| `master-rpc-handlers` | Master RPC handlers; layer 1: follower-servable methods, `NOT_THE_LEADER` |
| `master-catalog-entity` | Sys-catalog rows by `SysRowEntryType` (layer 1: parse and re-serialize) |
| `tools/conformance` (`master-conformance`) | Checks Rust re-serialization of real rows against C++ |
| `tools/tserver-sim` (`master-tserver-sim`) | Fake tservers that load a master: registration, heartbeats, tablet reports, tablet RPCs |
| `tools/tserver-sim-main` | The static library of the `master-tserver-sim` executable: flags and the C++ host bridge |
