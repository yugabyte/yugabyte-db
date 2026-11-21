# The master's C++ host

`src/yb/master_host/` holds the C++ side of the Rust master: `main()`, `MasterHost` and the
master's own bridge. The shared bridges are in `src/yb/rust_bridge/`
([src/yb/rust/docs/cpp-host.md](../../docs/cpp-host.md)).

## main()

`master_host_main.cc`:

1. `argv[0]` dispatch: a basename starting with `yb-tserver` runs `TabletServerMain` (LTO builds
   only).
2. `SetMasterFlagDefaults()` overrides server flag defaults.
3. `master_rs::register_flags()` registers the flags declared in Rust.
4. gflags parsing and glog setup (`MasterTServerParseFlagsAndInit`), then
   `SetDefaultInitialSysCatalogSnapshotFlags`.
5. `master_rs::run_master()`: creates the host, registers the Rust master services, starts the
   host (which raises the ulimits, starts the `TotalMemWatcher` thread and `MasterCallHome`),
   waits for SIGTERM and shuts down.

## The C++ master library

`src/yb/master` (CMake target `master`) holds only the C++ the host, MiniMaster, the tserver and
the tools need: the sys catalog and its apply-time code, the snapshot coordinator, AutoFlags,
encryption, the tablet service, the YCQL system table schemas, and helpers. The C++ catalog
manager is gone, but its gflags are not: a command line or flagfile written for the C++ master
must keep parsing, and the Rust master reads most of them by name. They are defined unchanged in
`src/yb/master/master_flags_<area>.cc`, and the metrics the host instantiates in
`master_metrics.cc`.

`build-support/check_master_flags.py` runs after every link of `yb-master` and fails the build if
`yb-master --helpxml` lacks a name in `src/yb/master/master_flags_baseline.txt`, the non-test,
non-deprecated gflags of the last C++ master. To retire one, make it a `DEPRECATE_FLAG` and remove
it from the baseline. There is no lint against including the C++ master's headers
(`catalog_manager.h`, `master.h`, ...): they no longer exist, so the compiler rejects it.

### Flags without effect

These parse but change nothing, because the Rust master has no such mechanism:

- `balancer_load_max_standard_deviation`, `retry_if_ddl_txn_verification_pending`,
  `wait_for_ysql_ddl_verification_timeout_ms`, `master_backup_svc_queue_length`: the C++ master
  did not read them either (the first only in a unit-test helper; the backup service used
  `master_svc_queue_length`).
- `use_cache_for_partitions_vtable`, `generate_partitions_vtable_on_changes`,
  `partitions_vtable_cache_refresh_secs`, `invalidate_yql_partitions_cache_on_create_table`:
  `system.partitions` is built on every read; there is no cache to refresh or invalidate.
- `list_snapshot_backoff_increment_ms`, `list_snapshot_max_backoff_sec`: only
  `SetupNamespaceReplicationWithBootstrap` polled snapshots with them, and it is not served.
- `batch_ysql_system_tables_metadata`: `CopyPgsqlSysTables` always writes a new database's
  catalog tables in one batch.
- `ysql_ddl_post_processing_failed_verification_retry_secs`: a failed DDL verification is retried
  on the verifier's next pass (about a second) instead of waiting in `kDdlPostProcessingFailed`.
- `sys_catalog_respect_affinity_task`: the sys catalog leader does not move to a master in a
  preferred zone or off the leader blacklist.
- `master_enable_deleted_tablet_cleanup`: there are no in-memory tablet maps to prune; DELETED
  tablets are read from the catalog.
- `hide_pg_catalog_table_creation_logs`, `master_bg_task_long_operation_warning_ms`,
  `master_ts_heartbeat_long_operation_warning_ms`, `master_log_lock_warning_ms`,
  `master_leader_lock_stack_trace_ms`, `tablet_split_rejected_candidates_log_interval_secs`,
  `xcluster_safe_time_log_outliers_interval_secs`, `xcluster_safe_time_slow_tablet_delta_secs`:
  they tune logs the Rust master does not write (`CreateTable` request dumps, long-operation and
  leader-lock warnings, rejected split candidates, safe-time outliers).

## MasterHost

`MasterHost` is a `DbServerBase` without a `CatalogManager`. It runs the parts of the C++ master
that live inside the sys-catalog tablet's Raft apply, because they change what is on disk on every
master and so must be the same code as on a C++ peer of the previous release during an upgrade:

- `SysCatalogTable`, through `SysCatalogContext` (the interface the C++ `Master` implements too).
- `MasterSnapshotCoordinator`, with a context whose `LeaderTerm()` is always -1, so only the
  apply-time paths run: `Load`, `ApplyWritePair`, and the `*Replicated` hooks. The tablet sees it
  through `SysCatalogTap`, which forwards every call and hands applied rows to Rust
  ([sys-catalog.md](sys-catalog.md)).
- PITR restores of the sys catalog through `RestoreSysCatalogOnApply`
  (`src/yb/master/sys_catalog_restore.cc`).
- `MasterAutoFlagsManager` for `auto_flags_config` and AutoFlags ops.
- The sys-catalog history cutoff: snapshot retention,
  `timestamp_syscatalog_history_retention_interval_sec`, CDCSDK safe time, and the YSQL history
  retention pin the leader publishes, which the host reloads every
  `catalog_manager_bg_task_wait_ms` (and, on the leader, the live pin; see Leading below).

It also runs the C++ `Webserver`, on loopback behind the Rust web port, with the pages a
follower master serves (see [web.md](web.md)).

It serves ConsensusService and RemoteBootstrapService in C++, and the remote-bootstrap steps for a
master added from shell mode are the same as `CatalogManager::StartRemoteBootstrap`.

**A new universe (K10).** With `--master_addresses` and no data, the host creates the sys-catalog
tablet (`SysCatalogTable::CreateNew`, after the same `CheckLocalHostInMasterAddresses` as
`CatalogManager::InitSysCatalogAsync`) and the first Rust leader writes the default rows
([catalog-state.md](catalog-state.md)). Only C++ can build the system tables' rows (their schemas
come from the YCQL virtual table classes), so `SystemTableRows` builds them with
`src/yb/master/system_tables.{h,cc}`. The host also gives the
default YCQL superuser's bcrypt hash and the YSQL major version. YSQL's initial sys catalog snapshot
is not restored yet (W9).

The master RPCs every C++ master serves from state the host has go to `MasterHost::HandleOnHost`
(`HOST_METHODS` in `master-rpc-handlers`): `RemovedMasterUpdate` (shell mode, as
`CatalogManager::GoIntoShellMode`), `CheckMasterTabletHealth`, the universe key RPCs (an
`EncryptionManager` of its own), `DumpSysCatalogEntries`, and `WriteSysCatalogEntry`
(`SysCatalogTable::ForceWrite` at the current term, only in emergency repair mode). In emergency
repair mode the Rust master loads no catalog and starts no leader term; the Raft leader reports
LEADER so yb-admin finds it for those two RPCs. As `CatalogManager` does once the sys catalog is
open (or after a remote bootstrap out of shell mode), the host asks the other masters for the
universe keys they hold in memory (`UniverseKeyClient`).

For the Rust catalog the host offers scans at a read time, the tablet's safe time, asynchronous
Raft writes in a term (`WriteSysCatalogRows`, the `SysCatalogTable::SyncWrite` request without
the wait), the Raft leader state, and the heartbeat fields its C++ objects own
(`HeartbeatHostParts`: universe key registry, snapshots info, AutoFlags config). On a leader it
also serves `GetAutoFlagsConfig` and `ValidateAutoFlagsConfig` through `HandleOnHost`, and the
AutoFlags changes (`PromoteAutoFlags`, `RollbackAutoFlags`, `PromoteSingleAutoFlag`,
`DemoteSingleAutoFlag`): `MasterAutoFlagsManager` submits the new config to the sys catalog
tablet in the Rust leader's ready term, and the Rust leader sends what C++ reads from `Master` for
the promotion checks (the live tservers' versions, whether a YSQL major upgrade is in progress).
It also serves
`GetFullUniverseKeyRegistry` and `IsEncryptionEnabled`, for which Rust passes the cluster config's
`EncryptionInfoPB` instead of the request. Tservers wait for `GetFullUniverseKeyRegistry` before
they start (with `allow_encryption_at_rest`, the default).

For table DDL the host runs the C++ steps that read or write engine formats, also through
`HandleOnHost`, with request and response messages in `master_rust_peer.proto`:
`PrepareTableSchema` (CREATE TABLE's schema, tablet count and partitions) and `AlterTableSchema`
(`ApplyAlterSteps`, `src/yb/master/table_ddl_util.cc`).
For YSQL it serves `GetYsqlCatalogConfig` (the database's version in `pg_yb_catalog_version`; Rust
resolves the namespace to its id first) and `GetYsqlYbSystemTableInfo` from the sys catalog. The
schedules that cover a namespace or table are not asked of the host's snapshot coordinator, which
never leads: the Rust leader derives them from the `SNAPSHOT_SCHEDULE` rows (`schedules.rs`).
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
targets, partition hashes and DNS lookups are C++'s. `system.partitions` is rebuilt on every
read.

**Leading.** Failure detection stays on as in C++, and an election only nudges the Rust leadership
loop ([leadership.md](leadership.md)). The host also does two leader duties for a Rust leader: it tells a master removed from the
quorum to go back to shell mode (`InformRemovedMaster`, a `RemovedMasterUpdate` call), and it holds
the pg catalog history back to the live YSQL history retention pin. The Rust leader aggregates the
pins from tserver heartbeats (`master-ts-manager` `pins.rs`), answers each heartbeat with the
cluster pins, publishes the oldest to the `HISTORY_RETENTION_PIN` row each
`catalog_manager_bg_task_wait_ms` when it changed, and hands it to the host
(`SetLiveYsqlHistoryPin`), which uses it for three intervals, so a master that stopped leading
falls back to the published pin.

**Snapshot operations.** The host's coordinator applies snapshot operations as a follower on every
Rust master: `SysCatalogTap` passes it leader term -1 even on the master that submitted them, and
the Rust leader does the leader's part (`master-snapshot-coordinator`,
[snapshot-coordinator.md](snapshot-coordinator.md)). `SubmitSnapshotOperation` submits an
operation (a serialized `TabletSnapshotOpRequestPB`) to the sys catalog tablet in the leader's
term, as `CatalogManager::Submit` does.

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

## Bridges

| Rust file | C++ side | Purpose |
|---|---|---|
| `master/server/src/ffi.rs` | `master_host.h`, `master_host_main.cc`, `sys_catalog_tap.h` | Entry points, `MasterHost` control, `MasterInfoHandle` for handlers and the store, the apply sink and write completions |

Handlers run on other threads while the main thread owns the host, so they get a
`MasterInfoHandle`, whose methods touch only state the host guards, rather than the host itself.

C++ strings that cross into Rust (status messages, flag values) go through
`rust::String::lossy`: `rust::Str` and `rust::String` throw on invalid UTF-8, and in a reactor or
flag callback that would end the process.
