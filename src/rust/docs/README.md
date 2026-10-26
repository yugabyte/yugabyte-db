# Rust in YugabyteDB

YugabyteDB components move to Rust one at a time, starting with the master (`master/`, see
[master/docs/README.md](../master/docs/README.md)). Each process stays C++ at the bottom: a C++
host owns the server base (FsManager, clock, Messenger, metric registry) and any storage engine,
and the Rust code on top talks to it through `cxx` bridges.

| Doc | Covers |
|---|---|
| [build.md](build.md) | How CMake drives Cargo, the toolchain, crates and checks |
| [cpp-host.md](cpp-host.md) | The shared C++ bridge code and how Rust logs and panics |
| [proto.md](proto.md) | Generated protobuf code and why it matches the C++ runtime byte for byte |

## Layout

| Path | Contents |
|---|---|
| `common/` | Crates any component uses. Named `yb-*`, never after a component. |
| `tools/` | Shared build tools |
| `master/` | The master's crates (`master`, `master-*`), tools and docs |
| `docs/` | Docs for the shared pieces |

| Crate | Owns |
|---|---|
| `common/yb-host-api` | Traits a C++ host provides to Rust, plus in-memory fakes for tests |
| `common/yb-cpp-host` | The cxx implementation of those traits; logging and panic hook |
| `common/yb-proto` | Messages and RPC service tables generated from `src/yb/**/*.proto` |
| `tools/yb-cxxgen` | Build tool: writes the C++ half of every bridge |

## Rules every crate follows

- Public APIs carry rustdoc (`missing_docs` is denied workspace-wide).
- Only `yb-cpp-host` and a component's static-library crate touch FFI. Everything else depends on
  `yb-host-api`, so `cargo test -p <crate>` needs no C++.
- A panic aborts the process (`panic = "abort"`) after logging through glog. Nothing may unwind
  across the C++ boundary.
- A crate that is not specific to one component goes in `common/` and has no component in its
  name, so the next component to move can use it as is.
