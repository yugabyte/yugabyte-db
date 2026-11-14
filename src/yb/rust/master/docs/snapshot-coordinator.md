# Snapshot coordinator (`master-snapshot-coordinator`)

Snapshots, snapshot schedules, restores, PITR, `ImportSnapshotMeta` and namespace clones on a Rust
leader (W10).

## Split with the C++ coordinator (K5)

Every master, C++ or Rust, runs the C++ `MasterSnapshotCoordinator`'s apply half: the
`CREATE_ON_MASTER`, `DELETE_ON_MASTER` and `RESTORE_SYS_CATALOG` operations and the `SNAPSHOT`,
`SNAPSHOT_SCHEDULE` and `SNAPSHOT_RESTORATION` rows. On a Rust master that coordinator never
acts as leader: `SysCatalogTap` passes it leader term -1 for the operations, even on the master that
submitted them, so it never sends tablet RPCs through its CatalogManager-shaped context.

This crate does what its leader does, from the same rows:

| Step | How |
|---|---|
| Create a snapshot | Entries collected from the catalog (`collect.rs`, `CollectTables`), or for a YSQL database as of the snapshot time from a sys catalog scan at that time; `CREATE_ON_MASTER` submitted through the host (`SubmitSnapshotOperation`) |
| Tablet snapshots | The controller sends `TabletSnapshotOp` (`CREATE_ON_TABLET`, `DELETE_ON_TABLET`, `RESTORE_ON_TABLET`, `RESTORE_FINISHED`) to tablet leaders through `TsRpcExecutor`, with C++'s retry and terminal-state rules (`tablet_ops.rs`) |
| Tablet states | Kept per term until all of an object's tablets are done, then written in one update of its row with the next `version`, as `UpdateSnapshot` writes it |
| Delete | `DELETE_ON_MASTER`, then `DELETE_ON_TABLET`s; the row is removed `snapshot_coordinator_cleanup_delay_ms` after every tablet is done |
| Schedules | Rows written through the catalog writer; the controller takes a snapshot each interval, deletes snapshots past the retention, and removes a deleted schedule's snapshots and then its row |
| Restore | Without a schedule: the restoration row written here. PITR: `RESTORE_SYS_CATALOG`, which every master applies with the C++ code and which writes the restoration row; the catalog reloads, and the controller restores the tablets the row lists, then writes the complete time on the restoration and the schedule in one write and sends `RESTORE_FINISHED` |
| Import | `ImportSnapshotMeta` (`import.rs`): YCQL namespaces, types and tables created; YSQL tables found by relfilenode or name; YSQL tablets repartitioned to the backup's partitions |

The Rust writer writes these rows with UPDATE statements: only the metadata column, which the C++
coordinator's `ApplyWritePair` loads like its own write pairs. An INSERT would add a liveness
column it cannot decode.

Heartbeat responses carry the host coordinator's schedules and restorations (`snapshots_info`).
Tservers re-check restorations only when `last_restorations_update_ht` changes, which the C++
coordinator sets only as leader (the host's sends it unset); the Rust heartbeat sets it to the later
of the latest restoration completion and the term's start, so tablets leave the restore state even
when `RESTORE_FINISHED` is lost.

What is in flight is per term (`TermState`) and derived from the rows when a term starts: a
tablet still in its object's initial state is sent again. That includes a schedule's
creating-snapshot marker, which C++ keeps across terms and had to heal after failovers.

Times follow C++: snapshot times and the restore-in-the-future check use `MaxGlobalNow`; tablet
RPCs propagate `Clock::Now`, which also measures expiry, schedule deletion and restoration
completion. Propagating `MaxGlobalNow` moves each tserver's clock ahead by the maximum skew on
every RPC until the tservers crash on clock skew.

Two rules keep a row from going backwards:

- A tablet outcome records the state it moves the tablet from (`Outcome::from`) and applies only
  to a tablet still in it. A create that finishes after `DELETE_ON_MASTER` reset the tablets to
  DELETING does not mark them COMPLETE.
- `DELETE_ON_MASTER` is submitted outside the writer, so its row write could race a writer
  update of the same row prepared from the version before it. Both hold the object
  (`lock_object`) for their duration.

## Hidden objects

A drop of a table that a snapshot schedule covers, or whose tablets a snapshot with a TTL holds,
or whose namespace a snapshot is being created of, hides the table instead of deleting it
(`TabletDeleteRetainerInfo`). The rules are this crate's `Retention` implementation, which
table-lifecycle and the split manager call:

| Object | Retired as |
|---|---|
| Dropped table (YCQL `DeleteTable`, YSQL drop or roll-forward) | HIDING; the table deleter hides its tablets (`hide_hybrid_time`, `retained_by_snapshot_schedules`, `DeleteTablet` with `hide_only`) and marks it HIDDEN. A colocated table's hosting tablets record the schedules instead |
| Split parent (`DeleteNotServingTablet`) | Hidden, not DELETED |

Each controller pass also runs `CleanupHiddenObjects`: a hidden tablet is deleted once no live
schedule that retained it can restore to before it was hidden and no snapshot holds it; a hidden
table whose own tablets are all deleted becomes DELETED; a hidden colocated table is removed from
its hosting tablets (`RemoveTableFromTablet`) and then DELETED.

## PITR on the Rust leader

`RestoreSnapshotSchedule` disables splitting for the restore, finds or takes a snapshot that
covers the time, and submits `RESTORE_SYS_CATALOG`. The restore marks tables created after that
time HIDING and their tablets DELETED. Its rows bypass the apply tap, so the catalog reloads; the
leader leaves Ready meanwhile, and on its return asks every tserver for a full tablet report, as
every C++ catalog load does, so the replicas of those tablets are deleted. The tablets are restored
once every live tserver has sent that report and the replica deletes went out (at most 10 s):
clients with a stale cache must not read a deleted split child after the restore completes. C++
gets the same order from its 5 s poll interval. After the tablets are restored: index backfills
resume from their rows, splitting is enabled again, and the database's OID counters are advanced
past every OID in use, hidden tables included (`AdvanceOidCounters`).

An automatic-mode xCluster target's sys catalog is not restored (its DDLs are rolled back by
replication); that check answers no until W11.

## Clones (`clone.rs`)

`CloneNamespace` follows `CloneStateManager`. The clone row (`CLONE_STATE`) and the source
namespace's next `clone_request_seq_no` are written together (CLONE_SCHEMA_STARTED). The snapshot is
the schedule's snapshot covering the clone time, with its entries replaced by the namespace, its
running visible tables and their active tablets as of that time, read from the snapshot's
sys catalog files (`GetBackupEntriesAsOfTime`). For YSQL a tserver runs `ClonePgSchema` first;
the rest runs once it answers. The snapshot is imported under the target name (`import.rs` in
clone mode: the target namespace by name, tablets CREATING with `created_by_clone`, no create
waits, schema versions past the source table's), an imported snapshot of the new tables is
created, the target tablets get the source tablets' Raft peers, and each source tablet's leader is
sent `CloneTablet` (CREATING). The controller restores the target snapshot to the clone time once
the target tablets run (RESTORING); when the restoration is done, YSQL clears every live
tserver's metacache for the new database and enables connections to it (RESTORED, then COMPLETE),
and YCQL is COMPLETE. A failure after the row is written marks the clone ABORTED.

The tablets and snapshots a clone needs between steps are kept for the term only; a clone not
done when its term ends is marked ABORTED by the next leader, as C++ aborts it on load. Not
ported: `CheckTabletLimits` (the tablet replica limits are not enforced on Rust masters).

## Host calls

| Method | Purpose |
|---|---|
| `SubmitSnapshotOperation` (bridge) | A snapshot operation in the leader's term |
| `ScanSysCatalog` after `SysCatalogSafeTime` | Rows as of the snapshot time |
| `GetBackupPgTableInfo` | `ListSnapshots(prepare_for_backup)`: a YSQL table's PG schema name and an index's `indisvalid` at the snapshot time |
| `HighestPreservableOids` | `AdvanceOidCounters`' reads |
| `CompareImportSchemas` | `ImportSnapshotMeta`'s schema check (`Schema::Equals` with its comparators) |
| `PgYbTablegroupOid` | The default tablegroup of a database, for legacy colocated backups |
| `ScanSnapshotSysCatalog` | A clone's entries: sys catalog rows as of a time from a snapshot's files |
| `CountPgYbMigrationRows` | A clone's check that no YSQL upgrade ran since the clone time |
| `ClosestLiveTserver` | The tserver that runs `ClonePgSchema` and `EnableDbConns` |
| `HybridTimeNow` | `Clock::Now`, or `Clock::MaxGlobalNow` with `max_global` |

## Tests

`cargo test -p master-snapshot-coordinator` runs the coordinator against a fake host that applies
snapshot operations as the C++ apply half writes their rows, and fake tservers: creation on every
tablet with one row write, retries of failures with no terminal state, a dropped table failing a
snapshot, deletion and cleanup, a tablet gone during deletion, schedules (one per namespace,
validated edits, deletion), a restore to completion, hidden-object cleanup, clones a previous
leader left unfinished being aborted, and the outcome and aggregation rules.

`yb-admin-snapshot-schedule-test` runs against Rust masters with `YB_RUST_MASTER=1`
([testing.md](testing.md)).
