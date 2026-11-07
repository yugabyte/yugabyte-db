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

YSQL databases and tables and indexes answer `NotSupported`. Roles, permissions and UDTs are `master-ycql`'s.

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
maximums), then, under the name lock, the duplicate name and the namespace state. The table and
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
  under-replicated, as in C++.
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
