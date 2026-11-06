# The shared C++ bridge

`src/yb/rust_bridge/` holds the C++ side of the bridges every Rust component uses. It is compiled
into an object library, `yb_rust_bridge_obj`, which each executable that hosts Rust links next to
its Rust static library (the two call each other). A component's own host, such as
`src/yb/master_host/`, adds its bridges and `main()`.

| Rust file | C++ side | Purpose |
|---|---|---|
| `common/yb-cpp-host/src/ffi/log.rs` | `log_bridge.{h,cc}` | glog for Rust `tracing` and panics |
| `common/yb-cpp-host/src/ffi/rpc.rs` | `rpc_bridge.{h,cc}` | Rust RPC services and outbound calls on the C++ Messenger |
| `common/yb-cpp-host/src/ffi/flags.rs` | `flags_bridge.{h,cc}` | gflags values, watches, and flags declared in Rust |

## Logging

Rust `tracing` events go to glog: ERROR, WARN and INFO map to the glog severities, DEBUG and TRACE
behave like `VLOG(1)` and `VLOG(2)` (`--v`). A Rust panic is logged at FATAL, which prints a stack
trace and aborts, like a failed `CHECK`.

## RPC

The bridge carries serialized messages only; C++ never knows their types.

- **Inbound.** `RegisterRustService` registers a `ServiceIf` under a wire service name with its
  method names, queue limit and priority, through the host's `RpcServerHandle` (the C++ server's
  `RegisterService` is protected). Each call reaches Rust as an `InboundCallHandle` with the
  method index and request bytes. It answers exactly once; if Rust drops it unanswered, the
  handle's destructor fails the call so the caller does not wait for its deadline.
- **In-process calls** skip serialization in the Messenger: the service gets the caller's request
  object. The bridge serializes it for Rust and parses Rust's response into the caller's response
  object.
- **Outbound.** `RpcClient::Call` sends serialized bytes through the host's proxy cache and
  completes a Rust future from the reactor thread.

## Flags

gflags stays the only flag registry. Flags declared in Rust with `yb_flag!` are registered at
startup through `DefineFlag`, before gflags parses the command line, so they appear in
`--helpxml`, flagfiles, `/varz` and `SetFlag` like flags defined in C++, with their yb tags.
`FlagRef` reads any flag by name, and `subscribe()` follows runtime changes through
`RegisterFlagUpdateCallback`.
