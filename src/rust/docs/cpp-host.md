# The shared C++ bridge

`src/yb/rust_bridge/` holds the C++ side of the bridges every Rust component uses. It is compiled
into an object library, `yb_rust_bridge_obj`, which each executable that hosts Rust links next to
its Rust static library (the two call each other). A component's own host, such as
`src/yb/master_host/`, adds its bridges and `main()`.

| Rust file | C++ side | Purpose |
|---|---|---|
| `common/yb-cpp-host/src/ffi/log.rs` | `log_bridge.{h,cc}` | glog for Rust `tracing` and panics |

Rust `tracing` events go to glog: ERROR, WARN and INFO map to the glog severities, DEBUG and TRACE
behave like `VLOG(1)` and `VLOG(2)` (`--v`). A Rust panic is logged at FATAL, which prints a stack
trace and aborts, like a failed `CHECK`.
