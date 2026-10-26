# The master's C++ host

`src/yb/master_host/` holds the C++ side of the Rust master: `main()`, `MasterHost` and the
master's own bridge. The shared bridges are in `src/yb/rust_bridge/`
([src/rust/docs/cpp-host.md](../../docs/cpp-host.md)).

## main()

`master_host_main.cc` keeps the C++ master's startup order:

1. `argv[0]` dispatch: a basename starting with `yb-tserver` runs `TabletServerMain` (LTO builds
   only), as `master_main.cc` does.
2. `SetMasterFlagDefaults()`, shared with the C++ master, overrides server flag defaults.
3. `master_rs::register_flags()` registers the flags declared in Rust.
4. gflags parsing and glog setup (`MasterTServerParseFlagsAndInit`).
5. `master_rs::run_master()`: creates the host, registers the Rust master services, starts the
   host (which raises the ulimits and starts the `TotalMemWatcher` thread, as `master_main.cc`
   does), waits for SIGTERM and shuts down.

`master_main.cc` steps not taken: `MasterCallHome`, which needs the C++ `Master`, and
`SetDefaultInitialSysCatalogSnapshotFlags`, which only matters to a master that creates a universe.

## MasterHost

`MasterHost` is a `DbServerBase` without a `CatalogManager`. It runs the parts of the C++ master
that live inside the sys-catalog tablet's Raft apply, because they change what is on disk on every
master and so must be the same code as on a C++ peer:

- `SysCatalogTable`, through `SysCatalogContext` (the interface the C++ `Master` implements too).
- `MasterSnapshotCoordinator`, with a context whose `LeaderTerm()` is always -1, so only the
  apply-time paths run: `Load`, `ApplyWritePair`, and the `*Replicated` hooks.
- PITR restores of the sys catalog through `RestoreSysCatalogOnApply`
  (`src/yb/master/sys_catalog_restore.cc`), shared with `CatalogManager`.
- `MasterAutoFlagsManager` for `auto_flags_config` and AutoFlags ops.
- The follower half of the sys-catalog history cutoff: snapshot retention,
  `timestamp_syscatalog_history_retention_interval_sec`, CDCSDK safe time, and the YSQL history
  retention pin the leader publishes, which the host reloads every
  `catalog_manager_bg_task_wait_ms`.

It also runs the C++ `Webserver`, on loopback behind the Rust web port, with the pages a C++
follower master serves (see [web.md](web.md)).

It serves ConsensusService and RemoteBootstrapService in C++, and the remote-bootstrap steps for a
master added from shell mode are the same as `CatalogManager::StartRemoteBootstrap`. It cannot
create a new universe: that would make it the first leader.

The master RPCs every C++ master serves from state the host has go to `MasterHost::HandleOnHost`
(`HOST_METHODS` in `master-rpc-handlers`): `RemovedMasterUpdate` (shell mode, as
`CatalogManager::GoIntoShellMode`), `CheckMasterTabletHealth`, the universe key RPCs (an
`EncryptionManager` of its own), `DumpSysCatalogEntries`, and `WriteSysCatalogEntry`, which fails as
on a C++ master outside emergency repair mode.

**Never leading.** The host turns off `enable_leader_failure_detection`, so it never starts an
election. If an explicit leader transfer elects it, the state-change callback steps it down at
once, retrying while the step-down is refused, and `GetMasterRegistration` reports FOLLOWER
meanwhile, as a C++ leader that is not ready does.

## Bridges

| Rust file | C++ side | Purpose |
|---|---|---|
| `master/server/src/ffi.rs` | `master_host.h`, `master_host_main.cc` | Entry points, `MasterHost` control, `MasterInfoHandle` for handlers |

Handlers run on other threads while the main thread owns the host, so they get a
`MasterInfoHandle`, whose methods touch only state the host guards, rather than the host itself.

C++ strings that cross into Rust (status messages, flag values) go through
`rust::String::lossy`: `rust::Str` and `rust::String` throw on invalid UTF-8, and in a reactor or
flag callback that would end the process.
