# CDC streams (`master-xrepl`)

The `CDC_STREAM` rows that xCluster and CDCSDK share, on a Rust leader (W11). C++ keeps maps of
streams by table and by replication slot; here they are read from the catalog snapshot.

| RPC | How |
|---|---|
| `GetCDCStream`, `GetCDCDBStreamInfo`, `ListCDCStreams` | From the rows; a stream being deleted is not found |
| `UpdateCDCStream` | The given entries replace the rows in one write; the xCluster target-applied filter and WAL anchor flags of a row stay as they are, and a `state` option is dropped (the state has its own field) |
| `DeleteCDCStream` | The streams are marked DELETING; an ACTIVE xCluster stream in WAL record format needs `force_delete` |
| `DisableDynamicTableAdditionOnCDCSDKStream` | Sets `cdcsdk_disable_dynamic_table_addition` on a live CDCSDK stream |
| `GetUDTypeMetadata` | The namespace is resolved here; the host reads pg_enum, or pg_class and pg_attribute for composite types |
| `GetTableSchemaFromSysCatalog` | The host reads the schema at `read_time` from the sys catalog, and the PG schema name then |
| `RemoveUserTableFromCDCSDKStream`, `RemoveTablesFromCDCSDKStream`, `ValidateAndSyncCDCStateEntriesForCDCSDKStream`, `CleanupStaleCDCStreams` | See [below](#tables-joining-and-leaving-streams) |
| `CreateCDCStream` (CDCSDK) | See below. Creating an xCluster stream answers NotSupported until the xCluster part of W11 |

Stream ids are 32 lower-case hex digits (`YB_STRONGLY_TYPED_HEX_UUID_IMPL`), unlike the dashed
snapshot and restoration ids; a request's id must have exactly 32.

## Creating a CDCSDK stream

`CreateNewCdcsdkStream`'s steps, in order:

1. `system.cdc_state` is created if missing (the host gives the create request).
2. The tables: the namespace's eligible tables (`IsTableEligibleForCDCSDKStream`), or the bound ones,
   plus the PG catalog tables a logical replication stream polls.
3. The row, INITIATED with a consistent snapshot option, else ACTIVE; a live stream already on the
   slot, or `max_replication_slots`, refuses it.
4. A `cdc_state` row per tablet with an invalid checkpoint, so a rollback finds them.
5. Retention barriers: each table's `wal_retention_secs` is written (no new schema version) and every
   tablet's leader gets `AlterSchema` with it, in batches of
   `cdc_create_stream_alter_table_dispatch_batch_size` tables with
   `cdc_create_stream_alter_table_dispatch_delay_ms` between them (`max_concurrent_alter_table_rpcs`
   is not applied). With a consistent snapshot option the request names
   the stream as retention requester, and the tablet answers with the op id the snapshot is safe
   from, which becomes its `cdc_state` checkpoint. Catalog tables live on the sys catalog tablet;
   the host submits their `CHANGE_METADATA_OP` and writes the row when it applies, which is waited
   for. A tablet's row is written before its alter returns, so step 6 finds a row still invalid only
   when writing it failed, and fails the create.
6. With a consistent snapshot option: one snapshot time (`MaxGlobalNow`) for every tablet, written
   on the row, which becomes ACTIVE; then the `cdc_state` rows get it, and the slot gets its row.

A failure after step 3 marks the stream DELETING.

The C++ test sync points of these paths (`CreateCDCSDKStream::k*`,
`AsyncAlterTable::CDCSDKCreateStream`, `CleanUpCDCStreamMetadata::*`,
`ProcessTablesToBeRemovedFromCDCSDKStreams::*`, `UpdateCDCProducerOnTabletSplit::*` and the
`Fail*` ones) are passed at the same steps through the host's `TestSyncPointCallback`, only under
`TEST_running_test`, so itests that inject failures or order themselves against the master with
`LoadDependency` drive a Rust leader as they drive C++.

With `ysql_yb_enable_implicit_dynamic_tables_logical_replication` the host registers the master's
CDC service (`MasterTabletServer::CreateCDCService`), through which logical replication reads the
PG catalog tables' changes from the sys catalog tablet. The controller enables it
(`EnableCDCService`) once a CDCSDK stream exists, as `SetCDCServiceEnabled` does.

## Tables joining and leaving streams

Dynamic table addition (`ProcessNewTablesForCDCSDKStreams`): each pass of the controller, an
eligible table of a live stream's namespace that the stream lists neither as qualified
(`table_id`) nor as unqualified (`unqualified_table_id`) and whose tablets all run joins it, up to
`cdcsdk_table_processing_limit_per_run` tables. C++ queues a table when it is created and rebuilds
the queue from the same rule at load, so a create needs no hook. For gRPC streams (or without
`ysql_yb_enable_replication_slot_consumption`) the table's WAL retention is set and its tablets
get `cdc_state` rows with invalid checkpoints; logical replication streams got theirs when the
tablets were created: `CreateTablet` carries `cdc_sdk_set_retention_barriers` and the replica's
`cdc_sdk_safe_op_id` becomes each stream's row (`PopulateCDCStateTableOnNewTableCreation`),
through `XreplHooks`, which table-lifecycle calls.

`RemoveUserTableFromCDCSDKStream` and `RemoveTablesFromCDCSDKStream` add eligible tables to
`unqualified_table_id`; a table in both lists is still to be removed, which is the rule C++ also
rebuilds its map from. The pass deletes its tablets' `cdc_state` rows (a colocated tablet's row
stays while another table of the stream is on it), then takes it out of `table_id`. A table a
stream has but may not have (an index or materialized view) leaves the same way. As C++ does when
it loads the streams (`FindAllNonEligibleTablesInCDCSDKStream`), those are looked for once per
leader term; tests rely on finding them only after a master restart.
`RemoveTablesFromCDCSDKStream` only deletes the rows of ineligible tables, from a map kept in
memory as in C++. `ValidateAndSyncCDCStateEntriesForCDCSDKStream` deletes the stream's rows of
colocated tables it no longer has and of existing tablets hosting none of its tables.
`CleanupStaleCDCStreams` reports, and unless a dry run deletes, the rows of streams that are gone
or being deleted and of tablets that are gone.

## Splits and dropped tables

A table in a CDCSDK stream can be split, unless `enable_tablet_split_of_cdcsdk_streamed_tables`
(or, for logical replication streams, `enable_tablet_split_of_replication_slot_streamed_tables`)
is off. When the children are registered, each stream gets a row
per child (`UpdateCDCProducerOnTabletSplit`): a logical replication stream continues from the
parent's checkpoint, the others from 0.0. `DeleteNotServingTablet` hides the parent instead of
deleting it, and the hidden-objects cleanup keeps it, until a pass of this controller releases it
(`DoProcessCDCSDKTabletDeletion`): for every stream of the table its row is gone, the stream never
read it, the stream reads both children, or the stream's slot restart time passed the hide time
(or its barriers expired, `cdc_intent_retention_ms`). The rows of streams done with it are
deleted. C++ keeps the retained parents in a map rebuilt at load; here the released ones are
kept in memory and a new leader recomputes them. A table in an xCluster stream is still not
split.

With `enable_table_rewrite_for_cdcsdk_table`, a dropped table that a logical replication stream
has (the old table of a rewrite, or any DROP) is hidden rather than deleted
(`CDCSDKPopulateDeleteRetainerInfoForTableDrop`, through the coordinator's `Retention`), and its
tablets are released by the same pass once each stream's row is gone or its slot passed the hide
time. The table stays in the streams until its tablets are deleted. Colocated tables are not
retained (C++ keeps them in `colocated_tables_retained_by_cdcsdk_`).

## Deleting

The leader's `xrepl` controller (`RunXReplBgTasks`) deletes the `cdc_state` rows of DELETING
streams through the host, then in one write takes xCluster streams out of the xCluster config's
paused streams and the outbound replication groups and deletes the rows.

The same controller removes dropped tables from CDCSDK streams (`CleanUpCDCSDKStreamsMetadata`).
C++ lists a dropped table in the stream's `dropped_table_id` when the table is dropped; each pass
here also finds the tables a stream names that have no row, are being deleted or are hidden, so a
drop needs no hook into table-lifecycle. The `cdc_state` rows of tablets that hold no other live,
eligible table of the stream are deleted first (never the slot's or the sys catalog tablet's), then
the tables leave the row in one write. A stream left in DELETING_METADATA by the old cleanup becomes
ACTIVE.

## Host calls

| Method | Purpose |
|---|---|
| `CdcStateCreateTableRequest` | `CDCStateTable::GenerateCreateCdcStateTableRequest` |
| `CdcStateUpsert`, `CdcStateRead`, `CdcStateDelete`, `CdcStateDeleteStreams` | `cdc_state` rows, through the host's `cdc::CDCStateTable`; a read without a stream is every row |
| `SetCatalogRetentionBarrier` | `SetAllInitialCDCSDKRetentionBarriersOnCatalogTable` |
| `ReadUDTypeMetadata` | `GetUDTypeMetadata`'s pg_catalog reads |
| `ReadTableSchema` | `SysCatalogTable::GetTableSchema` and `YsqlManager::GetPgSchemaName` at a read time |
| `EnableCDCService` | `MasterTabletServer::EnableCDCService` |
| `TestSyncPointCallback` | `TEST_SYNC_POINT_CALLBACK(name, &fail)`, for sync points whose callback takes a `bool*` |

## Tests

`cargo test -p master-xrepl`: stream lookups by id and slot, listing filters, updates keeping the
xCluster flags, deletion and cleanup, CDCSDK creation with and without a consistent snapshot, its rollback on an
injected failure, the schema and type RPCs, tables joining and leaving streams, stale rows, rows
for new tablets and split children, and a hidden split parent's release, against a fake host and
tserver.
