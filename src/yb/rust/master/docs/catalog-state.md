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

**Deadlines and lanes (K6).** `mutate_with` and `submit_with` take `WriteOptions`: the caller's
deadline, and a lane. A mutation still queued when its deadline passes is dropped unwritten with
`Expired` (`TimedOut` to the caller), since the caller has given up and a late write could surprise
its retry; once in a Raft write it can no longer be dropped. The caller gets `Expired` at the
deadline, not when the writer reaches the mutation: a flag set by whichever of the two claims the
mutation first keeps the writer from taking one its caller gave up on. Each batch takes the priority
lane first (lease grants, DDL completion), then the normal lane; mutations submitted while a batch
is being formed join it. Heartbeat reports pass the tserver's deadline.

Metrics: `rust_master_catalog_writer_queue_wait_us` (submit to joining a batch or being dropped),
`..._commit_latency_us` (Raft write sent to rows in the snapshot), `..._batch_rows`,
`..._queue_depth`, `..._conflicts` and `..._expired`.

Flags: `rust_master_catalog_writer_max_batch_rows` (1000), `..._max_batch_bytes` (4 MiB),
`..._max_inflight` (4 Raft writes).

## Default rows (K10)

`defaults::default_rows` builds one mutation with every row a catalog must have and lacks, as
`CatalogManager::VisitSysCatalog` writes them: the security, transaction-tables and YSQL catalog
configs, the YCQL system namespaces, the system tables and their tablets (built by the host), the
default `cassandra` role, the cluster config (with a universe uuid when
`master_enable_universe_uuid_heartbeat_check` is on), the xCluster config and the persistence block
map. The leader writes it on every election, in a ready hook with a catch-up token
([leadership.md](leadership.md)): a new universe gets everything in one Raft write, and an existing
one only what a newer release added or a flag now asks for (a universe uuid, the default role's
creation marker). With `ycql_allow_cassandra_drop`, the security config records that the default
role was created, so a dropped role stays dropped. A malformed `--cluster_uuid` fails the hook, as
it fails `VisitSysCatalog`. The universe uuid of an existing universe is also set by a leader-term
controller (`set_universe_uuid_if_needed`), since the AutoFlag can be promoted after the election.

## Self-check

`self_check` hashes the snapshot and a scan of the tablet at the snapshot's `as_of`. A mismatch
means a row reached the tablet without passing the tap: it is logged, counted
(`self_check_mismatches`) and fixed by a reload. Every master runs it every
`rust_master_catalog_self_check_interval_ms` (60 s; 0 disables).

## Testing

`cargo test -p master-catalog-state` runs the materializer and writer against `FakeSysCatalog`,
including `catalog_matches_store_under_random_workload`, a property test that mixes replicated
writes, local writes and restores and checks the catalog equals the store row for row.
