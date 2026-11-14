# CDC streams (`master-xrepl`)

The `CDC_STREAM` rows that xCluster and CDCSDK share, on a Rust leader (W11). C++ keeps maps of
streams by table and by replication slot; here they are read from the catalog snapshot.

| RPC | How |
|---|---|
| `GetCDCStream`, `GetCDCDBStreamInfo`, `ListCDCStreams` | From the rows; a stream being deleted is not found |
| `UpdateCDCStream` | The given entries replace the rows in one write; the xCluster target-applied filter and WAL anchor flags of a row stay as they are, and a `state` option is dropped (the state has its own field) |
| `DeleteCDCStream` | The streams are marked DELETING; an ACTIVE xCluster stream in WAL record format needs `force_delete` |
| `DisableDynamicTableAdditionOnCDCSDKStream` | Sets `cdcsdk_disable_dynamic_table_addition` on a live CDCSDK stream |
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
   tablet's leader gets `AlterSchema` with it. With a consistent snapshot option the request names
   the stream as retention requester, and the tablet answers with the op id the snapshot is safe
   from, which becomes its `cdc_state` checkpoint. Catalog tables live on the sys catalog tablet;
   the host submits their `CHANGE_METADATA_OP` and writes the row when it applies.
6. With a consistent snapshot option: one snapshot time (`MaxGlobalNow`) for every tablet, written
   on the row, which becomes ACTIVE; then the `cdc_state` rows get it, and the slot gets its row.

A failure after step 3 marks the stream DELETING.

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
| `CdcStateUpsert`, `CdcStateRead`, `CdcStateDelete`, `CdcStateDeleteStreams` | `cdc_state` rows, through the host's `cdc::CDCStateTable` |
| `SetCatalogRetentionBarrier` | `SetAllInitialCDCSDKRetentionBarriersOnCatalogTable` |

## Tests

`cargo test -p master-xrepl`: stream lookups by id and slot, listing filters, updates keeping the
xCluster flags, deletion and cleanup, and CDCSDK creation with and without a consistent snapshot
against a fake host and tserver.
