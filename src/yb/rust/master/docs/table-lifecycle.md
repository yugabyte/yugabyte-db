# Namespaces and tables (`master-table-lifecycle`)

The leader's namespace and table DDL (W7). Every DDL request is one catalog mutation; what follows
it (creating tablets on tservers, finishing the table) is a controller that works from the
persisted rows alone, so a new leader picks it up where the last one stopped (K1).

## Served so far

YCQL keyspaces and tables that are not indexes:

| RPC | Notes |
|---|---|
| `CreateNamespace` | One write of the RUNNING row. C++ writes PREPARING, then RUNNING; nothing happens in between for a keyspace. |
| `DeleteNamespace` | One write that removes the row, as C++ does. Refused while the keyspace has a table or a type, or a snapshot schedule covers it (`schedules.rs`, from the `SNAPSHOT_SCHEDULE` rows, since the host's coordinator can only collect entries through a leader `CatalogManager`). |
| `IsCreateNamespaceDone`, `IsDeleteNamespaceDone`, `ListNamespaces`, `GetNamespaceInfo` | From the snapshot. `ListNamespaces` orders by id; C++ uses hash-map order. |
| `ListTables`, `GetTableSchema` | From the snapshot, with C++'s visibility rules and relation types. |
| `CreateTable`, `IsCreateTableDone` | Below. |
| `AlterTable`, `IsAlterTableDone` | Below. |
| `DeleteTable`, `IsDeleteTableDone` | Below. Dropping an index on its own, and a table a snapshot schedule covers (which C++ hides rather than deletes), are not served yet. |
| `FlushTables`, `IsFlushTablesDone` | `flush.rs`, as C++'s `FlushManager`: one `FlushTablets` per tserver with a replica of the tables' (and with `add_indexes`, their indexes') tablets; a compaction records `last_full_compaction_request_time`. Not persisted, so a new leader answers NotFound for an old leader's request, as C++ does. |
| `TruncateTable`, `IsTruncateTableDone` | `truncate.rs`. As C++: `Truncate` to each tablet's leader, and a YCQL table's indexes too (PG truncates each YSQL index itself). Not persisted, so a new leader reports an unfinished truncate as done, as C++ does. A table in a CDCSDK stream is refused until W11. |

YSQL databases that are not colocated, and YSQL tables and indexes that are not colocated and in
the default tablespace (below), in a tablegroup or not. `AlterNamespace` renames a running
namespace. Tables of a legacy colocated database (colocated "via database", before tablegroups),
vector indexes and tablespaces answer `NotSupported`. Roles, permissions and
UDTs are `master-ycql`'s.

## YSQL tables and DDL verification (`ysql.rs`)

A YSQL `CreateNamespace` writes the database PREPARING (`next_normal_pg_oid` from the request, or
16384 for a copy, as the per-database OID allocator has it) and answers; clients poll
`IsCreateNamespaceDone`. A task then copies the template's PG catalog tables
(`ProcessPendingNamespace`): the host builds each new table's row as `CreateYsqlSysTableData`
does, adds them all to the sys catalog tablet in one `ChangeMetadata` and copies their rows
(`CopyPgsqlSysTables`), and the leader writes the rows, the sys catalog tablet's table ids and the
database RUNNING in one mutation, or FAILED. A new leader marks a YSQL database DELETING if it is
FAILED, or PREPARING and not being created in its term (a creation a leader change interrupted),
as C++'s namespace loader does, so the drop pass removes it; PG never committed it, so no client
can see it. A database whose next major version catalog was being made
(`NEXT_VER_PREPARING`) is left alone. The creating transaction is not verified yet.

A YSQL `CreateTable` names its table id (from the database and relation OIDs); it is unique by id,
not by name, since names are pg_class's. The host builds the row (`PrepareTableSchema` with the
namespace, `FillNewTableEntry`), so the YSQL-only fields (vector reverse mapping, matview,
`pg_table_id`, shared tables) stay C++'s. A replication info is refused: YSQL places tables by
tablespace.

A table a DDL transaction creates or drops carries the transaction and a
`ysql_ddl_txn_verifier_state` (`contains_create_table_op`, `contains_drop_table_op`, the
subtransaction) until the transaction ends. A drop in a transaction only marks the table, since a
statement may drop and create a table again. When PG reports the outcome
(`ReportYsqlDdlTxnStatus`), each table carrying the transaction keeps or undoes the change: a
committed drop and an aborted create become DELETING, anything else just loses the state.
An ALTER of a YSQL table in a DDL transaction records the table as it was (`previous_schema`,
`previous_table_name`) with `contains_alter_table_op`. An abort restores that as an alter of its
own; a commit drops the columns the transaction marked for deletion, also as an alter. Either way
`IsYsqlDdlVerificationDone` waits until the tablets have the alter, as C++ waits for the schema
version. Not done yet: rollback to a savepoint, and verifying a transaction whose outcome is never
reported (C++ polls the transaction status and compares the PG catalog).

Lookups follow `FindNamespace` and `FindTable`: by id, else by name (and database type, from
`GetDatabaseType`). A name is unique per database type; uniqueness is checked and written under a
leader-local lock, since a mutation can read the absence of a row but not of a name.

## CREATE TABLE

The schema steps that read or write engine formats run in the host's C++ (`PrepareTableSchema`,
shared with `CatalogManager` through `src/yb/master/table_ddl_util.cc`): `SchemaFromPB`, the key
column checks, column ids, the default partition schema, the tablet count (`RequestedNumTablets`,
from the live tservers of the table's placement) and the partitions. Rust does the rest in C++'s
order: namespace type, user-defined types, `max_create_tablets_per_ts`, the partitioning error,
the placement checks of `CheckValidPlacementInfo` (quorum of live tservers, block minimums and
maximums, and whether a quorum fits within each block's tservers and effective maximum), then, under the name lock, the duplicate name and the namespace state. The table and
all its tablets are written PREPARING in one mutation, with tablet ids sorted as C++ sorts them.

A transactional table needs `system.transactions`: if it does not exist, it is created first, as
`CreateGlobalTransactionStatusTableIfNotPresent` does, with one BINARY hash column `key` and
`transaction_table_num_tablets` tablets (or the per-tserver count). Its create mutation also bumps
`transaction_tables_config.version`, which C++ writes separately; tservers refresh their status
tablets when the version in their heartbeat response changes. `IsCreateTableDone` of a
transactional table waits for the transaction table too.

The placement check codes are dropped by C++ (they are set on a response it discards), so the
client gets `UNKNOWN_ERROR`; Rust answers the same.

## The `tablet-assignment` controller

Runs every `catalog_manager_bg_task_wait_ms` and on every catalog change, in each leader term:

- PREPARING tablets get replicas and become CREATING, one write per table. Replicas are the
  least loaded live, non-blacklisted tservers of the placement, by the table's then the cluster's
  replica count, then uuid, filling each placement block's minimum first, as
  `SelectReplicasForTablet` does. With fewer such tservers than replicas the tablet starts
  under-replicated, as in C++. Each read replica placement then adds OBSERVER replicas on its own
  tservers, counted in the same load (`BuildPlacementCandidates`); a read replica placement with
  no live tservers gets none, and the balancer adds them later.
- CREATING tablets get `CreateTablet` sent to each replica once per term (with the snapshot
  schedules that cover the table), through the paced
  executor (K2), retried until `tablet_creation_timeout_ms`. A new leader sends again, and so
  does the next pass after a call fails for good; a tserver that has the tablet answers
  AlreadyPresent, which counts as created. C++ instead waits for the timeout below.
- A CREATING tablet still not RUNNING `tablet_creation_timeout_ms` after its first send in the
  term becomes REPLACED, and a new PREPARING tablet takes its partition.
- Split children and cloned tablets are left alone: the source tablet's replicas create them.
  So are the tablets of a table being dropped.
- A PREPARING table whose active tablets all run becomes RUNNING. Report processing
  (`master-ts-manager`) turns each tablet RUNNING once a replica reports it with a leader.

Each tablet also gets a protege when it is assigned: the replica with the fewest proteges in the
table's batch (`SelectProtegeForTablet`). The `initial-elections` controller, run on every change
of the observed locations rather than on the interval, asks it to run for leader
(`RunLeaderElection` with `initial_election`) once a majority of the CREATING tablet's voters run
and no leader is known, once per term (`StartElectionIfReady`, `use_create_table_leader_hint`). If
the protege is gone or does not accept leader load under the cluster's leader affinity, the first
replica that does is asked. New tables so start with balanced leaders, and a table is created in
tens of milliseconds rather than waiting for Raft's election timeout.

The leader calls a tserver at the address `DesiredHostPort` picks: the broadcast address unless
`use_private_ip` allows the private one for the tserver's placement compared with this master's.
This master's placement comes from its registration, not the `placement_*` gflags: masters
embedded in one test process share the gflags, and MiniCluster blocks the address a server of
another placement group must not use, so a wrong choice leaves `CreateTablet` failing until the
creation timeout.

## ALTER TABLE

One mutation: the schema from the host's `ApplyAlterSteps` (`AlterTableSchema`), version + 1,
the new name, replication info or `wal_retention_secs`, and ALTERING, after C++'s checks in its
order (namespace running, name free, table visible and not PREPARING, `wal_retention_secs` alone).
The first change of an alter in flight copies the schema to the `fully_applied_*` fields. YCQL
clients get the latest schema from `GetTableSchema` anyway.

The `table-alters` controller sends `AlterSchema` (`ChangeMetadataRequestPB`) to each active
tablet's leader once per (tablet, table row version) in a term; the row version rather than the
schema version, since a `wal_retention_secs` alter keeps the schema version. When every tablet
has acknowledged it (an answer that the schema is already there, or newer, counts), it writes
RUNNING and clears the fully applied fields, if the row is still the one it sent. A tablet with
no known leader, or whose target answers that it lacks the tablet or does not lead it, is sent
again on the next pass to the leader the locations then name. C++ instead completes an alter from
tablet reports, and re-sends from heartbeats when a tablet reports an older version.

Only YCQL alters with no index permission change in flight are completed here. A YSQL table, or
one in a backfill's multi-stage alter (which a C++ leader may have started), stays ALTERING.

## DROP TABLE (K1)

A YCQL table in xCluster replication (an outbound xCluster stream, or an inbound stream in the
consumer registry) is refused unless `enable_delete_truncate_xcluster_replicated_table`. Then one
mutation sets the table and its indexes DELETING; the drop releases the name, since the name
index skips deleting tables. The `table-deletes` controller then, for each DELETING table, marks
its tablets DELETED in writes of at most 1000, queues `DeleteTablet` (`TABLET_DATA_DELETED`, with
`should_abort_active_txns`, which report-driven deletes also set when the table is gone or being
dropped, as `AsyncDeleteReplica` does) for every replica in the committed config or the observed locations,
and marks the table DELETED once no tablet is left. All of it is derived from the rows, so a new
leader finishes a drop the last one started; a replica missed here gets its delete from report
processing. C++ writes DELETING, then the tablets, then DELETED, with in-memory bookkeeping in
between (#33349).

## Colocation (`colocation.rs`)

A tablegroup is its parent table (`<tablegroup id>.tablegroup.parent.uuid`, or
`.colocation.parent.uuid` in a colocated database, whose tables PG puts in its default
tablegroup). `CreateTablegroup` creates it as C++ does, with the parent schema from the host, and
it owns one tablet marked colocated. It exists while its parent row is not being dropped; nothing
else is kept, so C++'s `YsqlTablegroupManager` has no counterpart. `ListTablegroups` lists the
parents; `DeleteTablegroup` drops the parent once the group holds no table that the same DDL
transaction is not dropping.

A table created with a `tablegroup_id` joins its group in its create mutation: it names the
parent as `parent_table_id`, owns no tablet, and gets a colocation id unique in the group (the
requested one, or random from 16384). It is created PREPARING; the `colocated-tables` controller
has the group tablet's leader add it (`AddTableToTablet`) and then marks it RUNNING, so a new
leader finishes an add the last one started (C++ keeps the task in memory only). A dropped
colocated table leaves the tablet (`RemoveTableFromTablet`) before the table-deletes controller
marks it DELETED, unless its parent is being dropped too. The alterer and the backfill reach a
colocated table through its parent's tablet (`CatalogSnapshot::hosting_tablets_of`), and the
alterer tracks acknowledgements per table and tablet, since colocated tables share one.
`GetTableSchema` gives a colocated table's `tablegroup_id`, from which PG places its indexes in
the same group.

Joining a group tablet whose row lists its tables (made before `use_parent_table_id_field`) is
refused for now.

## DROP DATABASE (`ysql.rs`, K1)

`DeleteNamespace` of a YSQL database checks what `DeleteYsqlDatabase` does (no snapshot schedule
covers it, no table in xCluster replication) and writes the database DELETING. The table-deletes
controller finishes every DELETING database each pass, so a new leader finishes a drop the last
one started:

1. Its PG catalog tables leave the sys-catalog tablet's Raft metadata, through the host
   (`DeleteYsqlSystemTables`, which skips a table already gone, so it can be repeated).
2. One write sets every table of the database DELETING, removes the catalog tables from the
   sys-catalog tablet's row and sets the database DELETED. C++ does these in two writes.
3. The tables' tablets are deleted as for DROP TABLE.

A database with a shared table goes to FAILED, as C++'s check fails it. Not done yet: dropping the
database's CDCSDK streams (a database with one is refused until W11), erasing its xCluster
persistence block, and removing the DELETED row on the next leader load (C++ keeps it as a
tombstone until then).

## YSQL indexes and backfill (`ysql.rs`, `backfill.rs`)

An index is created in one mutation with its indexed table, which gets the index's `IndexInfoPB`
as an alter (`PrepareAddIndexInfoToTable`). Unless backfill is off (`ysql_disable_index_backfill`)
or the request skips it (a non-concurrent build), the index starts at WRITE_AND_DELETE and its
table retains delete markers. PG's `BackfillIndex` moves it to DO_BACKFILL, another alter; once
every tablet has that version the alterer runs the backfill instead of returning the table to
RUNNING:

1. A backfill job is recorded on the indexed table (one index at a time, as C++ does for YSQL).
2. The read time is the latest `GetSafeTime` of the tablets, each at least the master's hybrid
   time now, persisted in the job so a new leader reuses it.
3. Each tablet's leader backfills in `BackfillIndex` chunks; each chunk's end is persisted in the
   tablet's `backfilled_until`, so a new leader resumes there.
4. On success each index stops retaining delete markers, records its birth time, and its tablets
   get `BackfillDone`; the index moves to READ_WRITE_AND_DELETE. On failure the index gets
   `backfill_error_message` and moves to WRITE_AND_DELETE_WHILE_REMOVING. Either way the job and
   the tablets' progress are cleared.

PG learns the outcome from the indexed table's schema or `GetBackfillStatus`. Dropping an index
also takes it out of its table's `indexes`, as an alter (`DeleteIndexInfoFromTable`). Not done
yet: the backfill's row counts and `GetIndexBackfillProgress`, the requester liveness check, and
xCluster's replicated backfill.
