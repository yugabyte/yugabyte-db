# Catalog state (`master-catalog-state`)

The in-memory catalog: built from the apply stream on every master, written by one writer on the
leader.

## Snapshot

`CatalogSnapshot` holds one persistent hash map (`imbl`) per entry type, plus indexes: tables by
`(namespace, name)` for those `FindTable` finds by name (not PGSQL, not deleting, not hidden), each
table's tablets by partition start (a table row never lists its tablets), and each parent table's
colocated children. A tablet with `hosted_tables_mapped_by_parent_id` lists no colocated tables;
`hosted_table_ids` derives them from the children index, as `TabletInfo::GetTableIds` does.

Hot fields are read from the row's top-level fields on the wire. The C++ master loads a row whose
nested `string` fields are not UTF-8 (it only logs); the Rust parser rejects it, so such a row
keeps its hot fields and only full parses fail. A `REDIS_TABLE_TYPE` table named `transactions`
is a transaction table, as `TableLoader::Visit` rewrites it. Snapshots are published through an `ArcSwap`; a reader's `Arc` never changes under it,
and publishing a batch costs O(changed rows * log n).

`active_tablets_of` returns, for each partition start, the deepest split that is not DELETED or
REPLACED, as `TableInfo::partitions_` holds them in C++.

## Materializer

`run_materializer` consumes the apply stream:

1. A load waits for the tablet's safe time T to pass every batch already consumed, notes the op
   index A of the last batch the tap sent, and scans every entry type at T. Batches at or below
   both T and A are in the scan and skipped; every other batch is applied. The op index matters
   because snapshot ops bypass MVCC and can apply at or below a safe time already reached.
2. Each later batch is applied to a copy and published as one snapshot.
3. A Reload (restore, remote bootstrap, shell mode, a self-check mismatch) starts a new load and
   a new `generation`. Batches and barriers that arrive while a load is pending wait for it, also
   when it fails and is retried.
4. Rows of an entry type this build does not know are skipped, as the C++ loader of this release
   skips them. A row that does not parse is kept undecoded, so the catalog still matches the
   tablet and readers that parse it fail. Only rows refused by design (D16, YEDIS) stop the
   process.

`Catalog::is_reloading` is true from the moment a Reload is sent until its load is published.

## Writer

`CatalogWriter` is the only path that writes. `mutate(&token, |snapshot| ...)` builds a
`Mutation` from the latest snapshot: the rows it read (with versions) and the rows it writes. The
writer validates mutations in order and sends the valid ones as one Raft write:

- the token must be current (`Leadership::check`), and no reload may be pending: the snapshot is
  then known to be stale;
- a mutation built by `mutate` must come from the current `generation`;
- every read version must equal the snapshot's, and no read or written row may be in a write
  still in Raft; otherwise the mutation conflicts and `mutate` rebuilds it from the next
  snapshot, for up to 10 s (a time budget, because under contention on one row only one writer
  wins each round);
- upserts of the bytes already there and deletes of missing rows are dropped, like the C++
  writer's skip of unchanged messages.

A write is acknowledged once its rows are in the published snapshot (write, then barrier), so the
caller's next snapshot contains it. A failed Raft write fails every mutation in its batch.

Flags: `rust_master_catalog_writer_max_batch_rows` (1000), `..._max_batch_bytes` (4 MiB),
`..._max_inflight` (4 Raft writes).

## Self-check

`self_check` hashes the snapshot and a scan of the tablet at the snapshot's `as_of`. A mismatch
means a row reached the tablet without passing the tap: it is logged, counted
(`self_check_mismatches`) and fixed by a reload. Every master runs it every
`rust_master_catalog_self_check_interval_ms` (60 s; 0 disables).

## Testing

`cargo test -p master-catalog-state` runs the materializer and writer against `FakeSysCatalog`,
including `catalog_matches_store_under_random_workload`, a property test that mixes replicated
writes, local writes and restores and checks the catalog equals the store row for row.
