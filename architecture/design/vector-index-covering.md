# Covering vector indexes

Status: draft.

## Goal

Support `CREATE INDEX ... USING ybhnsw (embedding vector_l2_ops) INCLUDE (category, price)`. Vector search then evaluates the pushed-down filter on the values stored in the vector payload, without reading the candidate rows from regular RocksDB. Result rows are still read, see Follow-ups.

Today a filtered search fetches the row of every candidate the filter checks (`PgsqlVectorFilter`), and `ExecuteBatchKeys` fetches the top-k rows again.

## DDL and catalog

- Set `amcaninclude = true` for ybhnsw (`ybvector.c`).
- PG passes the INCLUDE attnums in `YbcPgVectorIdxOptions`. `PgCreateTableBase::SetVectorOptions` maps them to column ids, as it does for the vector column, into a new `PgVectorIdxOptionsPB::covered_column_ids`. Primary-key columns are skipped because the ybctid already holds them. `IndexInfoPB::columns` can't carry them: master fills it only for non-YSQL indexes.
- When an index has covered columns, master forces `PgVectorIdxOptionsPB::store_payload`.
- The covered column set never changes during the index's life: dropping an INCLUDE column drops the index, and a type change rewrites the table. So the tablet builds a `Schema` -> `SchemaPacking` for the covered columns once, when it loads the index, and the payload needs no schema version.

## Payload format

Two new fields in `DocVectorIndexPayload`, after the ybctid:

- `0x04`: a varint size, then the covered columns packed with `RowPackerV2` against that packing, without the packed row type and schema version.
- `0x08`: the hybrid time the vector was written at (`VectorIndexesUpdater::write_time_`), for visibility checks without the row, see Read path. Backfilled vectors store the backfill read time, which is the index hybrid time (`DocVectorIndex::hybrid_time()`).

`DocVectorIndexPayloadYbctid` already skips trailing fields. `DecodeDocVectorIndexPayload` needs to learn the new flags.

## Write path

Invariant: a vector's payload matches the row's covered values for as long as the row references that vector id. So a stale payload can only belong to a dead vector, except for the known gap below.

- Write evaluation takes the tablet's vector indexes after the write permit (`PgsqlWriteOperation::SetVectorIndexes`).
- Insert: a table with a covering vector index always writes a packed row, whatever `ysql_enable_packed_row` says.
- Update that writes the vector or a covered column: `ApplyUpdate` already reads the row, with the covered columns added to its projection. It writes the covered columns the update does not, and when the update does not write the vector, rewrites it with the same data and a new `VectorId`, recording the removal of the old id as for any vector update.
  Cost: each such update is an HNSW insert, the old vector stays in the index (compaction keeps dead vectors of indexes that store payloads, #37974), and the tablet gets the vector deletion mark. Covering only makes sense for rarely updated columns.
- Apply (`VectorIndexesUpdater`): an index with covered columns takes a vector together with the covered values written to its row. They come from all records of the row in the batch, the packed row, columns that did not fit into it, and column updates, so they are collected per row, latest write wins, and the vector is inserted once the next row starts. A vector whose row misses covered values in the batch, from a write evaluated before the index was created, is inserted without the covered field, and search reads its row.
- Index creation: `AddTableToTablet` pauses write permits (`Tablet::PauseWritePermits`, as `AlterSchema` does) while it adds a covering index. With the pause, writes evaluated before it reach Raft before the index op, so their hybrid time is below the backfill read time and backfill covers them; writes evaluated after it see the index.
- Known gap: a covered column written without the vector leaves the vector with stale covered values and no new id. It comes from:
  - a transaction that updated only a covered column before the index was created and commits after it;
  - an update evaluated after the tablet dropped another covered column of the index and before it removed the index, see `UpdateCoveredVectorIndexes`;
  - possibly a write evaluated by a new leader that has not applied the index yet, since only the leader that adds the index pauses write permits.

  `Feed` detects it, and has to read the row and insert the vector again.

## Read path

Candidates are checked by the payload when every column of the pushed-down filter (a `col_ref` with a type) is a key column or a covered column, and the read time is not below the index hybrid time, the write time of backfilled vectors.

The result rows are still read. Postgres always requests the vector column: the `ORDER BY embedding <-> ...` expression stays in the scan target list, evaluated on the row (YB relations use no physical target list, so other columns are requested only when referenced). So the rows could come from the payload only once search also returns each result's vector, which the index stores as written; see Follow-ups.

For each candidate, `PgsqlVectorFilter` does:

1. Build a `PgTableRow` from the payload: key columns from the ybctid (`PgKeyDecoder`), covered columns from the new field (`PackedRowDecoderV2`).
2. Run the pushed-down filter on that row (`FilteringIterator::Match`). Rejecting needs no I/O and, except for the known gap in Write path, is safe: either the row fails the filter, or the vector is dead.
3. Reject rows that have intents, as today. `ReplaceIntentRows` re-adds them from the table.
4. Check that the vector is live at the read time:
   - The tablet has a vector deletion (`has_vector_deletion`): fetch the row and compare its vector id, as `PgsqlVectorFilter` does today.
   - No vector deletions: every vector visible at the read time is live. Compare the payload write time with the read time: reject a newer vector, and restart the read when it falls into the uncertainty window, as a row read would. No reads.

The deletion mark is sampled after `ReadIntentRowKeys` (`PgsqlReadOperationData::table_has_vector_deletion`), not when the read operation is created. Writes set the mark before the deletion becomes visible: a non-transactional write before its regular DB write, a transaction apply inside the apply write, before it is visible and before a later write removes its intents (`ApplyIntentsContext::Complete`). So a deletion is always seen either through the intents or through the mark, but only if the mark is read after the intents. The read query's `RequestScope` also keeps the intents of a transaction applied during the read until the read ends. A wrongly accepted candidate would not reach the results anyway: the final row fetch applies the filter again, and Postgres fetches another page when rows are filtered out.

| Filtered search | Today | Covered, no deletions | Covered, with deletions |
|---|---|---|---|
| Rejected candidate | row fetch | 0 reads | 0 reads |
| Accepted candidate | row fetch | 0 reads | row fetch |
| Top-k results | row fetch each | row fetch each | row fetch each |

`has_vector_deletion` is sticky: one delete, vector update or covered column update switches the tablet to row fetches for good. Until deleted vectors are tracked some other way (the same gap that keeps `vector_index_store_payload` off by default), checking accepted candidates without reads only works on insert-only tablets.

## Gating and upgrade

Creating a covering index needs the experimental `vector_index_enable_covered_columns` master flag. Before it is enabled by default, it needs a kExternal AutoFlag that gates index creation, not only payload writes: flags `0x04` and `0x08` must not be written before every tserver can read them, and an older tserver ignores `covered_column_ids`, so as a leader it would not give the vector a new id on an update of a covered column. That AutoFlag also guarantees that `ysql_skip_row_lock_for_update` is promoted, so an UPDATE locks the row. The covered columns and the vector it writes without naming them are not in its conflict set, so without the row lock a concurrent update of a covered column can be lost.

## Out of scope

- Covering the vector column itself.
- Planner costing and Index Only Scan.
- Tracking deleted vectors, so that tablets with deletions also skip the row fetch.
- xCluster. Its external apply records a deletion only after the regular DB write, which the zero-read path can't tolerate, so master rejects INCLUDE columns on a vector index of a replicated table. A table with such an index can still be added to replication later, so `Tablet::ApplyKeyValueRowOperations` sets the deletion mark before any xCluster write to a tablet with a covering index, and the target reads the rows of accepted candidates.

## Follow-ups

1. Covered rows need the vector column, see Read path. Either search returns the vector of each result from its chunk (ybhnsw has no `GetVector`, so the HNSW backends would return it with the result), or Postgres takes the ORDER BY distance from the index instead of the row, which is a planner change. Measured on 200k rows of 128 dimensions with a warm cache, reading the top 10 rows takes 0.5-5% of the query time, while a filter checked by the rows instead of the payload makes filtered queries 4-7 times slower. So this one has low priority.
2. The known gap in Write path: abort the tablet's transactions while a covering index is added (pause writes, wait for the applied safe time, `StopActiveTxnsPriorTo`, then the `AddTable` op), or read the row at apply.
3. Chunk memory budgets (`EstimateNumVectorsForBytes` for the mutable chunk, backfill and compaction chunks) do not count payload bytes, which were ybctid-sized before covered columns. Large covered values make chunks use several times the configured memory. Count payload bytes against the budgets.

## Tests

In `pg_vector_index-test.cc`, filters checked against the payload with the `vector_index_covered_checks` metric:

- every covered type, key columns and NULLs, with covered column updates and deletes: `CoveredColumnsBackfill`, `CoveredColumnsInsert`
- intent rows of the same and other transactions: `CoveredColumnsIntentRows`, `CoveredColumnsUnappliedUpdate`
- read restart from a payload write time in the uncertainty window: `CoveredColumnsReadRestart`
- writes concurrent with covering index creation: `CoveredColumnsConcurrentIndexCreation`
- tablet split: `CoveredColumnsSplit`
- colocated tables, dropped and duplicate covered columns: `CoveredColumnsColocatedColumnIds`, `CoveredColumnsDropColumn`, `CoveredColumnsDuplicate`

Not covered yet: compaction, and a delete followed by a search at an older read time.
