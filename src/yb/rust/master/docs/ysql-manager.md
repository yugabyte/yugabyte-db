# YSQL on the leader (`master-ysql-manager`)

What C++'s `YsqlManager`, `YsqlCatalogConfig` and `ObjectLockInfoManager` do for YSQL on a Rust
leader (W9).

## initdb

A Rust leader does not run initdb (`master_auto_run_initdb`). As a C++ master does, the first
leader of a new YSQL universe restores the shipped initial sys catalog snapshot
(`share/initial_sys_catalog_snapshot`), in its ready hook before it writes the default rows:

1. If the YSQL catalog config row says initdb is not done, the leader asks the host
   (`RestoreInitialSysCatalogSnapshot`). The host checks what
   `MaybeRestoreInitialSysCatalogSnapshotAndReloadSysCatalog` checks (`enable_ysql`, the
   snapshot path, `create_initial_sys_catalog_snapshot`, the instance metadata's
   `initdb_done_set_after_sys_catalog_restore`, `master_join_existing_universe`), submits the
   `RESTORE_ON_TABLET` operation and replays the snapshot's tablet metadata changes. The restore
   bypasses the apply tap, so the tap posts a catalog reload as it applies, on every master: a
   follower that leads next must not write default rows over a catalog it has not reloaded.
2. Once the catalog has reloaded, the default rows are written with `after_initial_snapshot`: the
   snapshot's cluster config, xCluster config and persistence block map belong to the universe
   that made it and are replaced, and `initdb_done` is set, all in the one default-rows write.

A crash between the two leaves initdb not done, so the next leader restores again, as in C++.
Unlike C++, a catalog with a YCQL keyspace outside the system ones is never restored: universes a
Rust leader created before this existed have no `initdb_done`, and the restore would wipe them.

`IsInitDbDone` answers from the YSQL catalog config row.

`yb-master-rs` calls `SetDefaultInitialSysCatalogSnapshotFlags` at startup, as `yb-master` does,
so the snapshot path defaults to the one shipped with the binary.

## YSQL operation leases (K3)

A tserver starts PG and keeps its sessions only while it holds a lease (`RefreshYsqlLease`). The
lease state of each tserver is the `lease_info` of its OBJECT_LOCK_ENTRY row; when the lease ends
on the master is kept in memory per term, and every lease gets a full TTL when a term begins, as
C++ resets them on load.

A refresh extends the lease, without a write, only when it is live, for the same tserver process
(`instance_seqno`), and the tserver reports the current epoch. Anything else grants a new epoch:
written in the writer's priority lane, answered with `new_lease` and the DDL locks held under live
leases (less releases in progress) and every tserver's lease epoch floor, which the tserver
bootstraps its lock table from. A spurious new lease restarts every PG session on that tserver, so
the rule is C++'s exactly. A process older than the lease, or one that relinquished it, is refused.

The `MasterYsqlLease` service runs on its own two-thread runtime, so refreshes never wait behind
heartbeats or DDL. The `ysql-lease-expiry` controller marks leases past their deadline as not
live (`object_lock_cleanup_interval_ms`), then releases the DDL locks of the epochs that ended.

## Global DDL object locks (K3)

`locks.rs` is C++'s `ObjectLockInfoManager` less the leases. A DDL's lock
(`AcquireObjectLocksGlobal`) is taken in three steps:

1. In the master's own lock manager, the C++ `TSLocalLockManager` the host keeps
   (`AcquireLocalObjectLocks`). It holds only DDL locks, so a conflicting DDL waits there.
2. In the requesting tserver's OBJECT_LOCK_ENTRY row, under its lease epoch, transaction and
   subtransaction, so a tserver granted a lease later bootstraps with it.
3. On every tserver with a live lease (`AcquireObjectLocks`), with each one's
   `recipient_lease_epoch` and every tserver's lease epoch floor. A tserver that loses its lease
   meanwhile is skipped; INVALID_YSQL_LEASE and ShutdownInProgress are retried. Each RPC gets
   five seconds past its wait (`kTserverRpcsBufferForTimeoutDetection`): a tserver ends a lock
   wait after `refresh_waiter_timeout_ms` to recheck the transaction, and answers with an error if
   a deadlock aborted it. Without the buffer that answer is lost to the RPC timeout, the acquire
   is retried until the client's deadline, and the aborted DDL keeps the master's lock, blocking
   every conflicting DDL behind it. If a lease
   changed during the fan-out, the others still hold that tserver's old floor, so it is sent again
   to all with fresh floors.

A release (`ReleaseObjectLocksGlobal`) is persisted in the row as in progress and answered; it is
then sent to every live lease and removed from the row and the master's lock manager. A new
leader rebuilds its lock manager from every persisted lock (`ResetLocalObjectLocks`, before the
term is ready) and relaunches the releases in progress. The locks of a lease epoch that ended are
released by the master, with `ignore_lease_epochs_before` so tservers refuse that epoch's late
acquires. `WaitForLockersMultipleGlobal` is sent to every live lease. A release does not carry
catalog versions yet; tservers get them from the next heartbeat. DDL verification does not yet
hold back a release of the transaction it verifies.

## YSQL on the sys catalog tablet

PG reads and writes its catalog on the leader's sys catalog tablet through
`TabletServerService`. The host registers the C++ `MasterTabletServiceImpl` over
`MasterTabletServer`, both of which now take a `MasterTabletServerContext` instead of the C++
`Master`. It also registers `PgClientService` over `MasterTabletServer`, as C++ does, because
initdb (including the major upgrade's) runs PG with this master as its tserver. The host's context:

- serves only in the term the Rust leader reported ready in (`SetLeaderReadyTerm`, sent on every
  leadership phase change), as `ScopedLeaderSharedLock` checks `leader_ready_term_`;
- reads catalog versions from `pg_yb_catalog_version`, and skips version checks until that table
  is readable (initdb done) and during a YSQL major upgrade;
- refuses the protobuf catalog version, and gates catalog writes as
  `IsWriteToCatalogTableAllowed` does (below).

Tablet locations of system tablets (the sys catalog tablet and the YCQL system keyspaces' virtual
tables) are the masters, with the current leader, from the quorum's committed consensus state, as
`BuildLocationsForSystemTablet` does.

Heartbeat responses carry the per-database catalog versions and invalidation messages, unless the
tserver's fingerprint matches. With `enable_heartbeat_pg_catalog_versions_cache` (the default)
they come from a cache in the host, as C++ keeps one in `CatalogManager`: the `pg-catalog-versions`
controller refreshes it every half heartbeat interval (`pg_catalog_versions_cache_retry_ms` after a
failed read), and so does each `ReportYsqlDdlTxnStatus`, so a DDL's new version goes out with the
next heartbeat. The invalidation messages are re-read only when the versions changed. The host
resets the cache whenever the ready term changes, and drops a refresh read before the reset or
older than what it holds. The tablet service's version reads that allow the cache use it too.

## YSQL major upgrade (`major_upgrade.rs`)

`MajorUpgrade` is `YsqlInitDBAndMajorUpgradeHandler`. Its state is the YSQL catalog config's
`ysql_major_catalog_upgrade_info`, moved by `kAllowedTransitions` (DONE -> PERFORMING_INIT_DB ->
PERFORMING_PG_UPGRADE -> MONITORING -> DONE, a running state -> FAILED or PERFORMING_ROLLBACK,
FAILED -> PERFORMING_ROLLBACK -> DONE). The upgrade is in progress while that `catalog_version` is
not this build's YSQL major version. Rust keeps the state machine and the order of the steps; what
runs a process or works on the sys catalog tablet's YSQL rows runs on the host:

| Step | Where |
|---|---|
| Abort the sys catalog's transactions, then copy `pg_yb_catalog_version` from the prior version's table and empty `pg_yb_invalidation_messages` (`UpdateCatalogVersions`) | host `YsqlMajorUpgradeCatalogStep` |
| initdb in major upgrade mode, keeping the oids of template0 and yugabyte | host `RunYsqlInitDb` (`PgWrapper::InitDbForYSQL`) |
| pg_upgrade against the closest live tserver's postgres | host `RunYsqlPgUpgrade` (`master::PerformYsqlPgUpgrade`, shared with the C++ handler) |
| Finalize and rollback version checks of the live tservers | host `ValidateTserverVersions` |
| New-version catalog tables, pg_upgrade's drop and re-create of each database (`ysql_next_major_version_state`), rollback and the old catalog's cleanup | `master_table_lifecycle::major_upgrade` |

The leader sends the host the state (`SetYsqlMajorUpgradeState`) before its term is ready and
after each change. The host then reads PG catalog tables through the prior version's ids
(`GetVersionSpecificCatalogTableId`), skips catalog version checks, and allows only forced catalog
writes, never one to `pg_yb_catalog_version`. A new leader fails an operation the last one was
running, and starts an unfinished cleanup of the old catalog again.
A registering tserver whose YSQL major version the state does not allow (`ValidateTServerVersion`)
gets a fatal heartbeat error.

`ClientUpgradeYsql` (`yb-admin finalize_upgrade` ends with it) is sent on, through the host
(`ForwardUpgradeYsql`), to `UpgradeYsql` of the live tserver pg_upgrade would pick. At startup the
host removes the pg_upgrade socket directory a killed master left behind
(`CleanupStalePgUpgradeSocketDir`), as `Master::Init` does.

## YSQL system tables

The `ysql-system-tables` controller creates what `YsqlManager::RunBgTasks` does, except during a
YSQL major upgrade: `system.pg_advisory_locks` (`num_advisory_locks_tablets` tablets, all four
columns hashed) and, with `ysql_enable_auto_analyze_infra`, the auto analyze service's
`system.pg_auto_analyze_table`, a one-tablet table that hosts the `PG_AUTO_ANALYZE` stateful
service. `GetStatefulServiceLocation` answers with the tserver leading that tablet. The LISTEN/NOTIFY
objects (`ysql_yb_enable_listen_notify`, off by default) are not created.

`ReservePgsqlOids` reserves the next OIDs of a database's normal or secondary space from its
namespace row (`next_normal_pg_oid`, `next_secondary_pg_oid`), as C++ does; the response carries
the cluster config's `oid_cache_invalidations_count`.

## system.transactions

With YSQL on, the `transaction-table` controller creates `system.transactions` once
`max(txn_table_wait_min_ts_count, replication_factor)` tservers have registered, as C++'s leader
does from its tserver count callback. The transaction tables version is bumped when a status table
is created, once for each of its tablets that turns RUNNING in a report, and once for each status
table deleted: tservers refresh their usable status tablets only on a version change, and the
first bump comes before any tablet runs. Tests count on one bump per tablet, as C++ intends.

## Backends catalog version (`backends.rs`)

`WaitForYsqlBackendsCatalogVersion` answers how many PG backends in the cluster still run on a
catalog version of a database older than the requested one; online DDL (CREATE INDEX between its
steps) asks until the answer is 0. As C++'s `YsqlBackendsManager`: the requested version must not
be past the master's (`pg_yb_catalog_version`, through the host); one job per (database, version)
asks each tserver with a live YSQL lease (`WaitForYsqlBackendsCatalogVersion` on the tserver),
again while it reports lagging backends; a tserver whose lease ends counts as caught up. A caller
waits until the client's deadline less
`wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms` and gets the total, or -1
while a tserver has not answered. A version found reached is remembered for the term.
