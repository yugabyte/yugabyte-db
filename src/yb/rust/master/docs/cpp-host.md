# The master's C++ host

`src/yb/master_host/` holds the C++ side of the Rust master: `main()`, `MasterHost` and the
master's own bridge. The shared bridges are in `src/yb/rust_bridge/`
([src/yb/rust/docs/cpp-host.md](../../docs/cpp-host.md)).

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
  apply-time paths run: `Load`, `ApplyWritePair`, and the `*Replicated` hooks. The tablet sees it
  through `SysCatalogTap`, which forwards every call and hands applied rows to Rust
  ([sys-catalog.md](sys-catalog.md)).
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
master added from shell mode are the same as `CatalogManager::StartRemoteBootstrap`.

**A new universe (K10).** With `--master_addresses` and no data, the host creates the sys-catalog
tablet (`SysCatalogTable::CreateNew`, after the same `CheckLocalHostInMasterAddresses` as
`CatalogManager::InitSysCatalogAsync`) and the first Rust leader writes the default rows
([catalog-state.md](catalog-state.md)). Without `--TEST_rust_master_allow_leadership` it refuses:
such a master never starts an election. Only C++ can build the system tables' rows (their schemas
come from the YCQL virtual table classes), so `SystemTableRows` builds them with
`src/yb/master/system_tables.{h,cc}`, which `CatalogManager` uses too. The host also gives the
default YCQL superuser's bcrypt hash and the YSQL major version. YSQL's initial sys catalog snapshot
is not restored yet (W9).

The master RPCs every C++ master serves from state the host has go to `MasterHost::HandleOnHost`
(`HOST_METHODS` in `master-rpc-handlers`): `RemovedMasterUpdate` (shell mode, as
`CatalogManager::GoIntoShellMode`), `CheckMasterTabletHealth`, the universe key RPCs (an
`EncryptionManager` of its own), `DumpSysCatalogEntries`, and `WriteSysCatalogEntry`, which fails as
on a C++ master outside emergency repair mode.

For the Rust catalog the host offers scans at a read time, the tablet's safe time, asynchronous
Raft writes in a term (`WriteSysCatalogRows`, the `SysCatalogTable::SyncWrite` request without
the wait), the Raft leader state, and the heartbeat fields its C++ objects own
(`HeartbeatHostParts`: universe key registry, snapshots info, AutoFlags config). On a leader it
also serves `GetAutoFlagsConfig` and `ValidateAutoFlagsConfig` through `HandleOnHost`, and
`GetFullUniverseKeyRegistry` and `IsEncryptionEnabled`, for which Rust passes the cluster config's
`EncryptionInfoPB` instead of the request. Tservers wait for `GetFullUniverseKeyRegistry` before
they start (with `allow_encryption_at_rest`, the default).

For table DDL the host runs the C++ steps that read or write engine formats, also through
`HandleOnHost`, with request and response messages in `master_rust_peer.proto`:
`PrepareTableSchema` (CREATE TABLE's schema, tablet count and partitions) and `AlterTableSchema`
(`ApplyAlterSteps`), which share `src/yb/master/table_ddl_util.cc` with `CatalogManager`.
For YSQL it serves `GetYsqlCatalogConfig` (the database's version in `pg_yb_catalog_version`; Rust
resolves the namespace to its id first) and `GetYsqlYbSystemTableInfo` from the sys catalog. The
schedules that cover a namespace or table are not asked of the host's snapshot coordinator: it
collects a schedule's entries through `CatalogManager`, which is never leader on a Rust master,
so the Rust leader derives them from the `SNAPSHOT_SCHEDULE` rows (`schedules.rs`).
`EncodedPartitionKeys` gives a split the DocDB-encoded keys of its children's partition keys
(`PartitionSchema::GetEncodedPartitionKey`).

**YCQL virtual tables.** `system.local`, `system.peers`, `system.partitions`,
`system_schema.*` and `system_auth.*` (`SystemTableDefs`) are read by tservers from the leader
master's tablet service like any tablet. The host's `GetSystemTablet` serves them with a
`SystemTablet` per tablet (`ycql_virtual_tables.cc`) whose rows are formatted as the C++
`YQL*VTable` classes format them. What they show comes from Rust on every read
(`VirtualTables::Input`, a `YcqlVirtualTableInputPB`): the live tservers in uuid order, the
running YCQL keyspaces, the YCQL tables visible to clients with their tablets' locations, the
UDTs and the roles. The split is where the formats are: the schemas, CQL type names, index
targets, partition hashes and DNS lookups are C++'s, and the C++ classes read them through
`CatalogManager`, which a Rust master's host does not have. `system.partitions` is rebuilt on
every read rather than cached as C++ caches it.

**Leading.** Only with `--TEST_rust_master_allow_leadership`
([leadership.md](leadership.md)): failure detection stays on and an election only nudges the
Rust leadership loop. Without the flag the host turns off `enable_leader_failure_detection`, so it
never starts an election; if an explicit leader transfer elects it, the state-change callback
steps it down at once, retrying while the step-down is refused, and `GetMasterRegistration`
reports FOLLOWER meanwhile, as a C++ leader that is not ready does.

A Rust leader leaves these C++ leader duties undone, so leading stays test-only: snapshot
schedules do not take snapshots and restores are not served (the coordinator's `LeaderTerm()` is
-1), AutoFlags are not promoted, a removed master is not told (`InformRemovedMaster`), and the
YSQL history retention pin is not published.

## Embedded in a test process

`src/yb/master_host/embedded_master.cc` runs the Rust master inside a MiniCluster test (D12).
MiniMaster cannot link the host, which depends on the `master` library, so the host registers a
factory in `src/yb/master/embedded_master_if.h` (`RegisterEmbeddedRustMaster`) that MiniMaster
calls for each Rust master. The factory builds the host from MiniMaster's `MasterOptions`
(`CreateMasterHostWithOptions`) and hands it to `master_rs::start_embedded`, which runs the same
startup as `run_master` minus the wait for SIGTERM; `stop_embedded` shuts it down.

An embedded host (`SetEmbedded`) differs from the binary's in what a process has only one of, or
what would block the test:

- It opens the sys catalog on a thread of its own. `SysCatalogTable::CreateNew` waits for every
  peer, and MiniCluster starts the masters one after another.
- It does not raise ulimits or start the `TotalMemWatcher` and its `TerminationMonitor` (one per
  process).
- `TEST_rust_master_allow_leadership` is set, process-wide, so a cluster of Rust masters elects one.

## Bridges

| Rust file | C++ side | Purpose |
|---|---|---|
| `master/server/src/ffi.rs` | `master_host.h`, `master_host_main.cc`, `sys_catalog_tap.h` | Entry points, `MasterHost` control, `MasterInfoHandle` for handlers and the store, the apply sink and write completions |

Handlers run on other threads while the main thread owns the host, so they get a
`MasterInfoHandle`, whose methods touch only state the host guards, rather than the host itself.

C++ strings that cross into Rust (status messages, flag values) go through
`rust::String::lossy`: `rust::Str` and `rust::String` throw on invalid UTF-8, and in a reactor or
flag callback that would end the process.
