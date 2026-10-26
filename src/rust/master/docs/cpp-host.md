# The master's C++ host

`src/yb/master_host/` holds the C++ side of the Rust master: `main()` and the master's own bridges.
The shared bridges are in `src/yb/rust_bridge/` ([src/rust/docs/cpp-host.md](../../docs/cpp-host.md)).

## main()

`master_host_main.cc` keeps the C++ master's startup order:

1. `argv[0]` dispatch: a basename starting with `yb-tserver` runs `TabletServerMain` (LTO builds
   only), as `master_main.cc` does.
2. `SetMasterFlagDefaults()`, shared with the C++ master, overrides server flag defaults.
3. gflags parsing and glog setup (`MasterTServerParseFlagsAndInit`).
4. `yb::master_rs::run_master()` from the Rust static library.

## Bridges

| Rust file | C++ side | Purpose |
|---|---|---|
| `master/master/src/ffi.rs` | called from `master_host_main.cc` | Entry points into Rust |
