# Vector Index Design

This document describes the design and implementation of vector indexes in YugabyteDB: the
`ybhnsw` access method exposed through the pgvector extension in YSQL, and the distributed
approximate nearest neighbor (ANN) search engine that backs it in DocDB.

## 1. Overview

Vector indexes accelerate queries of the form:

```sql
SELECT ... FROM t ORDER BY embedding <-> '[...]' LIMIT k;
```

where `<->` is a distance operator over the pgvector `vector` type. Exact evaluation would scan
the whole table; the vector index answers such queries approximately using HNSW (Hierarchical
Navigable Small World) graphs.

HNSW graphs do not map naturally onto DocDB's key-value model: graph traversal is random-access,
the graph must be memory resident (or paged efficiently), and HNSW does not support deletion.
Because of this, vector indexes are not stored as DocDB rows. Instead each tablet hosts a
purpose-built storage engine, the Vector LSM: a log-structured collection of immutable HNSW graph
chunks, co-managed with the tablet's regular and intents RocksDB instances.

Layering, top to bottom:

```
  PostgreSQL           ybhnsw access method (pgvector extension, src/ybvector/)
      |
  pggate               PgsqlReadRequestPB.vector_idx_options
      |
  PgClientSession      VectorIndexQuery: fan-out to all tablets, merge, paging
      |
  Tablet               TabletVectorIndexes: per-tablet index set, backfill, lifecycle
      |
  DocDB adapter        DocVectorIndex: ybctid reverse mapping, filters, frontiers
      |
  Vector LSM           mutable chunk + immutable chunks + manifest + compactions
      |
  ANN backends         usearch | hnswlib (in-memory build), yb_hnsw (block-based serving)
```

Key source locations:

| Directory | Contents |
|---|---|
| `src/postgres/third-party-extensions/pgvector/src/ybvector/` | `ybhnsw` index access method |
| `src/yb/yql/pggate/` | request construction, DDL options plumbing |
| `src/yb/tserver/pg_client_session.cc` | cross-tablet fan-out, merge, paging |
| `src/yb/tablet/tablet_vector_indexes.*` | tablet component: index set, backfill |
| `src/yb/docdb/doc_vector_index.*` | DocDB adapter over the Vector LSM |
| `src/yb/docdb/rocksdb_writer.*` | write path: insert on apply |
| `src/yb/docdb/pgsql_operation.cc` | read path: per-tablet ANN search |
| `src/yb/vector_index/` | Vector LSM engine, interfaces, manifest |
| `src/yb/ann_methods/` | usearch / hnswlib / yb_hnsw wrappers |
| `src/yb/hnsw/` | block-based HNSW format and block cache |
| `src/yb/dockv/doc_vector_id.*` | vector id and reverse mapping encoding |

## 2. YSQL layer

### Access method

The `ybhnsw` index access method is registered by the vendored pgvector extension. All
YB-specific access method code lives in the extension under `src/ybvector/`:

- `ybvector.c` builds the shared `IndexAmRoutine`.
- `ybhnsw.c` defines the `ybhnsw` handler, its reloptions and GUCs.
- `ybvectorread.c` implements `ambeginscan` / `amrescan` / `amgettuple` / `amendscan`.
- `ybvectorwrite.c` implements build / backfill / insert / delete callbacks.
- `ybdummyann.c` defines `ybdummyann`, an unordered test-only access method.

When `CREATE INDEX ... USING hnsw` targets a YB relation, `DefineIndex` silently substitutes
`ybhnsw` (the same way `gin` is substituted with `ybgin`), so upstream pgvector DDL works
unchanged.

Operator classes are registered for the `vector` type only:

| Operator class | Operator | Distance |
|---|---|---|
| `vector_l2_ops` (default) | `<->` | L2 (Euclidean, squared internally) |
| `vector_ip_ops` | `<#>` | negative inner product |
| `vector_cosine_ops` | `<=>` | cosine distance |

Each opclass declares its operator `FOR ORDER BY` and provides the distance function as support
procedure 1.

### Planner and executor

Planning uses the stock PostgreSQL ordered-index-scan machinery: `amcanorderbyop = true` lets
`match_pathkeys_to_index` recognize `ORDER BY embedding <op> query` against the opclass and build
an index path with `indexorderbys`. There is no dedicated cost model yet: `ybvectorcostestimate`
delegates to `genericcostestimate`.

The `LIMIT k` value is captured by the Limit node into `yb_exec_params.limit_count`. During
`amrescan` it becomes the ANN prefetch size pushed to DocDB. The query vector is bound as a
constant, and its dimensions are validated against the index column typmod at bind time.

Scans always report that recheck may be required (`yb_ammightrecheck` returns true); recheck is
all-or-nothing per scan, decided by `YbNeedsPgRecheck`. Order-by recheck is never requested: the
returned order is trusted.

### Restrictions

- Single key column only (`amcanmulticol = false`).
- Partial vector indexes are rejected: the predicate is not pushed down to the access method, so
  allowing them would silently return wrong results.
- The indexed column must have explicit dimensions in its type modifier.
- Only the `vector` type is indexable; `halfvec`, `sparsevec` and `bit` have no `ybhnsw`
  opclasses.
- No index-only scans (see copartitioning below), no bitmap scans, no parallel scans, no backward
  scans.
- Vacuum callbacks are no-ops that emit warnings; cleanup happens inside DocDB (see Filters and
  Compactions).

## 3. Creation-time and search-time options

### Creation-time options

`CREATE INDEX ... USING ybhnsw (col vector_l2_ops) WITH (m = ..., m0 = ..., ef_construction = ...)`

| Option | Default | Range | Meaning |
|---|---|---|---|
| `m` | 32 | 5..64 | max neighbors per node on non-base levels |
| `m0` | 0 (falls back to `m`) | 0..256 | max neighbors per node on the base level |
| `ef_construction` | 200 | 50..1000 | candidate list size during graph construction |

The distance kind comes from the operator class. These options flow through pggate DDL calls
(`YBCPgCreateIndexSetVectorOptions`, `YBCPgCreateIndexSetHnswOptions`) into
`PgVectorIdxOptionsPB` inside the index's `IndexInfoPB`:
`{dist_type, idx_type, dimensions, column_id, hnsw{m, m0, ef_construction, backend}, id}`.

The master fills two fields the client does not set:

- `id`: a permanent identifier (the postgres table oid at creation time) that names the index's
  storage directory on every tablet and stays stable across backup/restore and clone.
- `backend`: the ANN backend, chosen from the `--vector_index_backend` gflag (default
  `yb_hnsw_hnswlib`). Supported values: `usearch`, `hnswlib`, `yb_hnsw`/`yb_hnsw_usearch`,
  `yb_hnsw_hnswlib`. The `yb_hnsw_*` values mean "build chunks in memory with usearch/hnswlib,
  persist and serve them in the yb_hnsw block format" (section 9).

### Search-time options

| Option | Default | Scope | Meaning |
|---|---|---|---|
| `hnsw.ef_search` GUC | 40 | 1..1000, per session/statement | beam width of the base-layer search |
| prefetch size | `LIMIT k` | per query | number of top results requested per round trip |

Both are pushed per-rescan into `PgVectorReadOptionsPB` on the read request:
`{vector, prefetch_size, num_top_vectors_to_remove, hnsw_options{ef_search}}`.
`num_top_vectors_to_remove` is a paging cursor, described in section 13. The `hnsw` and `ybhnsw`
GUC prefixes are reserved; the historical `ybhnsw.ef_search` name was replaced by
`hnsw.ef_search`.

## 4. Copartitioning model

A `ybhnsw` index is copartitioned with its base table: index data is stored on the same tablets
as the base rows, as a colocated cotable of the base table's tablet. This holds even when the
base table itself is not colocated; vector indexes are the only case where a cotable is attached
to a non-colocated table's tablets.

Consequences:

- There is no separate distributed index table and no cross-tablet index maintenance writes.
  Feeding a vector into the index is a tablet-local side effect of the base row write.
- The postgres-side `yb_aminsert` / `yb_amdelete` callbacks are no-ops for copartitioned
  indexes; DML maintenance happens entirely inside DocDB (section 10).
- Index build from postgres reduces to waiting for DocDB-side backfill readiness
  (`YBCPgWaitVectorIndexReady`); there is no row-by-row postgres-driven backfill.
- Every search must fan out to all tablets of the base table (section 13).
- Index-only scans are disabled (`amcanreturn` returns false): the index does not store a
  separately readable copy of the row.

In tablet metadata the index appears in the colocated table list, and `TableInfo::IsVectorIndex()`
identifies it via the presence of vector index options in its `IndexInfo`.

## 5. Data model and reverse index

### Vector ids

Every indexed vector is assigned a `VectorId`, a random UUID generated on the write path when the
row value is encoded. Vector ids are never reused: an update of the vector column produces a new
`VectorId`, and a delete simply retires the old one. This invariant makes cross-chunk result
merging and asynchronous cleanup sound: a vector id seen anywhere always denotes the same vector
value.

The vector column value is stored in the base row encoded together with its `VectorId`
(`dockv::DocVectorValue`), so both the regular DocDB row and the vector index refer to the same
id.

The id is generated when the write request is prepared, before Raft replication, and travels
inside the replicated write batch. All replicas of a tablet therefore see the same `VectorId` for
the same row version, and the graphs they build independently agree on vector identity.

### Reverse index (VectorId -> ybctid)

ANN search over HNSW chunks yields vector ids, but postgres needs base rows. The reverse mapping
is stored in the tablet's regular RocksDB (not in the vector index files) under keys of the form:

```
kVectorIndexMetadata kVectorId <16-byte VectorId> <DocHybridTime>  ->  ybctid | kTombstone
```

The mapping is MVCC just like normal DocDB data: an entry is written at insert time, and a
tombstone is written when the row is deleted or the vector column is updated. Because it lives in
the regular DB, it is covered by the tablet WAL, flushes, compactions, snapshots and remote
bootstrap with no extra machinery.

There are two ownership modes for who writes the mapping:

- Legacy: the vector index write path emits reverse mapping entries for each index (driven by the
  index list at apply time).
- Table-owned (`--enable_table_owned_vector_reverse_mapping`, default false): the table schema
  packing itself knows its vector columns and every insert/update writes the mapping, regardless
  of which indexes exist. The property is fixed at table creation
  (`TablePropertiesPB.owns_vector_reverse_mapping`), preserved by backup/restore, and allows
  backfill to skip reverse mapping writes entirely.

The reverse mapping serves three purposes: resolving search results to ybctids, filtering deleted
vectors at read time (section 11), and deciding which vectors are dead during chunk compaction
(the merge filter).

## 6. Vector LSM

`VectorLSM<Vector, DistanceResult>` (`src/yb/vector_index/vector_lsm.*`) is the storage engine.
One instance exists per vector index per tablet, with storage directory `vi-<options.id>` under
the tablet's rocksdb directory. It follows the LSM pattern of RocksDB but
stores HNSW graphs instead of sorted runs, and deliberately mirrors RocksDB's flush/frontier
contract so the tablet can manage it like just another DB.

### Chunk management

Data lives in chunks:

- One mutable chunk: an in-memory HNSW index (usearch or hnswlib) accepting inserts. Its
  capacity is bounded by `--vector_index_initial_chunk_size` (default 100000 vectors), or by a
  memory budget during backfill (section 14).
- Immutable chunks, each progressing through the states
  `kInMemory -> kOnDisk -> kInManifest`. Chunks carry a monotonically increasing `order_no`
  (logical age) and a unique `serial_no` (file name `vectorindex_<serial>.<ext>`, extension
  `.usearch`, `.hnswlib` or `.yb_hnsw` by backend).

When an insert would exceed the mutable chunk's capacity, the chunk is rolled: it becomes
immutable and is scheduled for saving, and a fresh mutable chunk is created. `Flush()` performs
the same transition on demand.

Inserts are asynchronous. An insert call splits the batch into subtasks of
`--vector_index_task_size` (16) vectors executed on a dedicated insert thread pool, with
back-pressure at `--vector_index_max_insert_tasks` (1000) outstanding tasks. A chunk counts its
in-flight tasks; the save of a rolled chunk starts only after its last insert task completes.

### Metadata and durability

The Vector LSM has no WAL of its own. Durability comes from two artifacts:

- Immutable chunk files, written once and never modified.
- An append-only manifest: files named `<n>.meta` containing length-prefixed, CRC32C-protected
  `VectorLSMUpdatePB` records. Each record adds and/or removes chunks; a record may carry a
  `reset` flag that logically truncates all prior state (used by full compactions to start a
  fresh manifest). Every append is fsynced.

Each chunk record persists the chunk's user frontiers (smallest/largest `ConsensusFrontier`,
serialized as protobuf `Any`), so the manifest also records how far the tablet's Raft log has been
made durable in this index.

Manifest updates are strictly ordered: a chunk enters the manifest only after all chunks with
smaller `order_no` are on disk, preserving the invariant that the manifested prefix of the chunk
sequence is gap-free. On open, the manifest files are replayed in order and all referenced chunk
files are loaded in parallel on the insert thread pool. Anything that was only in the mutable
chunk at crash time is lost from the LSM and re-materialized by tablet WAL replay (section 15).

A single sticky failure status makes the engine fail-stop: once a background insert or save
fails, subsequent operations return that error.

## 7. Compactions

Background compactions merge immutable chunks to bound chunk count and physically remove dead
vectors. Selection reuses RocksDB's universal compaction heuristics, applied over contiguous
manifested chunks:

1. Size amplification: if the older chunks dominate total size beyond
   `--vector_index_compaction_size_amp_max_percent` (200), merge everything.
2. Size ratio: merge a window of similarly sized adjacent chunks
   (`--vector_index_compaction_size_ratio_percent` = 20, minimum window 4).

Background compaction triggers when at least `--vector_index_files_number_compaction_trigger` (5)
chunks exist. Selected chunks are locked with a CAS-based compaction scope so concurrent picks
never overlap. A merging iterator walks input chunks in order, applies the merge filter (see
below) to discard dead vectors, and re-inserts survivors into one or more output chunks (split by
`--vector_index_compaction_chunk_max_mem_store_size_mb` when set). Frontiers of inputs are
carried over to outputs, including from frontier-only (empty) chunks.

Manual (full) compaction merges all chunks and writes a manifest `reset` snapshot into a new
manifest file. Compactions run as tasks on the shared priority thread pool and cooperatively
yield to flushes. Input chunk files become obsolete once the manifest swap completes and are
deleted when their last reference is released.

### Merge filter: VectorMergeFilter

During chunk merge, `VectorMergeFilter` (`src/yb/docdb/doc_vector_index.cc`) checks each input
vector id against the reverse mapping in the regular DB and discards ids with no live mapping.
This is the mechanism that physically reclaims deleted/updated vectors; it is the vector-index
analogue of compaction garbage collection, and the reason vacuum is unnecessary.

## 8. Single chunk representation and search

Chunks implement a common interface, `VectorIndexIf` (`src/yb/vector_index/vector_index_if.h`):
`Reserve`, `Insert`, `Search`, `GetVector`, iteration, `SaveToFile`, `LoadFromFile`, plus
`EstimateNumVectorsForBytes` for memory-budgeted sizing. A `VectorIndexFactory` closure created in
`docdb/doc_vector_index.cc` captures the backend choice and HNSW options.

Three backends exist under `src/yb/ann_methods/`:

- `UsearchIndex` wraps `unum::usearch::index_dense_gt`, keyed directly by `VectorId`. Files are
  loaded with a memory-mapped view. Searches are bounded by a counting semaphore sized to
  `--vector_index_concurrent_reads`.
- `HnswlibIndex` wraps `hnswlib::HierarchicalNSW`. Native hnswlib distance spaces are used where
  they exist (L2, inner product); for distances hnswlib lacks (cosine, and inner product on
  non-float coordinate types) usearch's punned metric is plugged in as the distance function.
- `YbHnswIndex` is read-only and file-backed (section 9): it represents only immutable chunks.
  The mutable chunk is always built by usearch or hnswlib and converted to the block-based
  format while being saved to disk.

A shared CRTP base (`IndexWrapperBase`) implements the common concurrency and lifecycle logic:

- While a chunk is mutable, inserts and searches are separated by a two-group mutex: multiple
  inserts run concurrently, multiple searches run concurrently, but the groups exclude each
  other.
- `SaveToFile` marks the chunk immutable; from then on searches take no locks at all.
- For the `yb_hnsw_*` backends, `SaveToFile` does not serialize the in-memory structure; it
  converts it into the yb_hnsw block format and returns a reopened `YbHnswIndex`, so the
  in-memory graph is released after flush.
- Block cache reservation (section 18) is taken during `Reserve` to account the in-memory graph
  against the shared block cache budget.

The mutable-chunk engine choice matters mostly for build performance and memory; the serving
format is dictated by the backend suffix. The default backend builds with hnswlib and serves via
yb_hnsw.

## 9. Block-based HNSW (yb_hnsw)

`src/yb/hnsw/` implements YugabyteDB's own immutable, block-paged on-disk HNSW representation.
Motivation: usearch/hnswlib formats are designed to be fully memory resident (or naively mmapped);
yb_hnsw makes the graph demand-paged through a shared cache with explicit accounting, so many
large chunks can be served within a fixed memory budget.

### File format

A file is a sequence of variable-length blocks followed by a footer. Block boundaries are not
stored inline; the footer holds the block table:

```
[aux data blocks][non-base layer blocks: level max..1][base layer blocks][vector data blocks]
[footer: version, header, cumulative block end offsets, crc32, footer size]
```

The header records dimensions, vector record size, entry point, max level, connectivity config
(m, m0), block sizing parameters and per-layer info. Blocks are limited by
`--yb_hnsw_max_block_size` (default 64 KB). All multi-byte values are little-endian; there is no
compression and no alignment (reads go through misaligned-safe loads). Files are written to a
temporary name and renamed atomically.

During conversion, vectors are reordered by descending graph level, so vectors present on level L
occupy a prefix of the id space. This makes level membership implicit and keeps upper-level
blocks dense. All neighbor references are rewritten in terms of these new 32-bit slot numbers.

Per-vector data is a fixed-size record: the location of its base-layer neighbor list (block +
begin/end), the location of its auxiliary data (the external 16-byte `VectorId`), and the raw
float coordinates. Non-base layers pack up to 65536 vectors per block, each block holding a
`uint16` offset array followed by the concatenated neighbor lists; the base layer stores plain
neighbor arrays addressed from the vector records.

### Import

`YbHnsw::Import` converts a built usearch or hnswlib index through a small adapter interface that
exposes levels, keys, coordinates and neighbor lists of the source graph. This runs at chunk save
time (flush or compaction output). The result is immediately queryable; whether the freshly
written blocks stay cached is controlled by `--yb_hnsw_keep_new_blocks_in_cache` (default false).

### Search

Search is our own implementation of the HNSW algorithm, written directly on top of this block
representation (no library code is involved at query time). It follows the standard two phases:
greedy descent from the entry point through the non-base levels, then a beam search on the base
level using a candidate min-heap, a bounded result heap and a visited set. The user filter
(section 11) is applied against the decoded `VectorId` during traversal.

Per-search scratch (heaps, visited set, block pin set) lives in a `YbHnswSearchContext`. Contexts
are pooled per index in a lock-free stack, so steady-state searches allocate nothing. Blocks
touched during a search are pinned in a per-search cache and released together when the search
ends.

### Block cache

A single process-wide `hnsw::BlockCache` (created per tablet server) wraps the shared
`rocksdb::Cache` used by RocksDB block caching, charging vector index blocks under the
multi-touch id. Each open index file owns a `FileBlockCache` with one `CachedBlock` entry per
block:

- `Take(block)`: refcounted pin. On miss, the first caller reads the block from the file and
  inserts it into the RocksDB cache with a retained handle; concurrent callers wait on a shared
  future, so each block is read once.
- `Release(block)`: on last unpin, the RocksDB handle is released, making the block evictable.
- Eviction is entirely delegated to the RocksDB cache's LRU/multi-touch policy. When RocksDB
  evicts a block that is still pinned by a search, the bytes stay alive until the last unpin.

Thus the vector index shares one memory budget with the RocksDB block caches, and cold indexes
naturally fall out of memory.

## 10. Insert on apply (write path)

Vectors enter the Vector LSM when the corresponding write is applied to the regular RocksDB, as
part of the same batch application, not on memtable flush and not from postgres-side index
maintenance.

- Transactional writes (the normal YSQL case): the intents-apply path (transaction commit
  applying provisional records) feeds the machinery per applied intent.
- Non-transactional writes: the batch writer iterates the write pairs while writing them to the
  regular DB. For YSQL this covers the autocommit single-row fast path (which bypasses the
  transaction apply flow), opt-in non-transactional writes (`yb_disable_transactional_writes`,
  non-transactional COPY) and xCluster external writes applied on the target cluster.

Both paths drive `VectorIndexesUpdater` (`src/yb/docdb/rocksdb_writer.*`):

1. `Feed(key, value)` decodes each applied record: tombstones are skipped, packed rows (V1/V2)
   are unpacked to locate vector columns, and non-packed per-column writes are matched by column
   id.
2. For each vector value it accumulates a `DocVectorIndexInsertEntry` per target index and emits
   the reverse mapping entry (`VectorId -> ybctid`) into the same regular-DB write batch. In
   table-owned reverse mapping mode the mapping is emitted from the schema packing for every
   vector column, independent of the index list.
3. `Complete()` calls `DocVectorIndex::Insert(batch, frontiers)` for each index, inserting into
   the mutable chunk with the operation's `ConsensusFrontier` so the LSM tracks the applied
   Raft index.

Two gates control whether a given apply feeds a given index:

- Creation-time gate: an index only ingests writes with commit hybrid time greater than the
  index's creation hybrid time (older rows arrive via backfill). xCluster targets are an
  exception and always apply.
- Replay gate: during tablet bootstrap a per-operation `StorageSet` says which storages
  (regular DB, each vector index) still need the operation (section 15).

Deletes and updates never touch HNSW chunks directly (HNSW cannot delete). They tombstone the
reverse mapping entry of the retired `VectorId`, and set a `has_vector_deletion` marker in the
consensus frontier. That marker, persisted and restored with the frontier, is what tells the read
path that the "no deletions ever happened" filter fast path is no longer valid.

Large transactions applied out-of-band are not yet supported by the vector index apply path (a
known TODO in `rocksdb_writer.cc`).

## 11. Filters

Read-time filtering solves two problems: MVCC visibility (an HNSW chunk may return vectors whose
rows were deleted or updated) and pushed-down WHERE clauses. A related filter applied during
chunk merge, `VectorMergeFilter`, is described in section 7.

`PgsqlVectorFilter` (`src/yb/docdb/pgsql_operation.cc`) is invoked inside the HNSW traversal for
every candidate vector id, before it can enter the result set:

- It resolves the `VectorId` through the reverse mapping reader. No live mapping (absent or
  tombstoned at the read time) means the row is gone: the candidate is skipped, and the beam
  search continues, so the query still returns k live results.
- If the request carries a pushed-down WHERE expression, the filter fetches the base row and
  evaluates the condition. It also verifies the row's stored `VectorId` matches the candidate,
  which guards against a ybctid being reused with a new vector value.

Filter construction is skipped entirely on two fast paths:

- `--vector_index_skip_filter_check` (test/escape hatch), or
- the index has never seen a deletion (`has_vector_deletion` frontier flag is clear), the request
  has no WHERE filter, and `--vector_index_no_deletions_skip_filter_check` (default true).

When the filter is skipped, the search result may contain dead ids; the final ybctid resolution
pass drops entries whose mapping is missing (the response is simply allowed to have fewer rows,
which the fan-out layer compensates for).

## 12. Tablet-level read

On the tablet, `PgsqlReadOperation::Execute` dispatches to `ExecuteVectorLSMSearch` when the
request carries `vector_idx_options`:

1. The effective result size is `max_results = num_top_vectors_to_remove + prefetch_size`
   (see read paging in section 13).
2. Search is refused until the index reports `BackfillDone` (section 14).
3. Candidates are gathered from every place a vector can live at that moment:
   - the Vector LSM chunks: `DocVectorIndex::Search` snapshots the mutable chunk plus all
     immutable chunks and queries each as an HNSW index (concurrently readable) with
     `{max_num_results = max_results, ef = ef_search, filter}`;
   - the insert registry: vectors sitting in not-yet-executed insert tasks are brute-force
     scanned, so a completed insert is searchable before its background tasks run;
   - the intents DB: committed transactional writes that have not been applied to the LSM yet
     are re-scanned and their distances computed directly, so vectors written by a transaction
     become visible right after commit, exactly as regular records do.
4. The per-source results are merged and deduplicated. Duplicates are normal: a vector travels
   through the sources above as it progresses from write to durability, and at any moment may be
   present in a couple of them at once. Results are ordered by `(distance, vector_id)`;
   deduplication by id is correct because ids are never reused.
5. The merged list is sorted by distance, the first `num_top_vectors_to_remove` entries are
   dropped, and up to `prefetch_size` rows are fetched by ybctid (each vector id resolved
   through the reverse mapping) and returned together with their encoded distances
   (`vector_index_distances`, `vector_index_ends`) and a `vector_index_could_have_more_data`
   flag.

Unlike normal scans, a vector read returns no per-tablet paging state; paging is coordinated one
level up (section 13).

## 13. Distributed read

### Parallel read from tablets

Vectors are distributed across tablets by the indexed table's partitioning, which is driven by
the primary key and not by the vector values: closeness in vector space says nothing about
tablet placement, and the top-k vectors of a query are usually spread evenly over all tablets.
There is no effective way to predict where they are, so every tablet has to run an independent
search and the per-tablet results are merged into the global top-k - the same merge pattern the
tablet itself applies one level below when combining its sources (section 12).

Vector reads are excluded from the normal pggate parallel-scan machinery and executed by a
dedicated `VectorIndexQuery` object in `PgClientSession`
(`src/yb/tserver/pg_client_session.cc`):

1. `Prepare` snapshots the table's partition list (with its version) and issues one cloned read
   op per partition, in parallel, each bound to its partition key.
2. `ProcessResponse` collects every partition's `{distance, row}` results into a single pool,
   sorts by distance, and returns the global top `prefetch_size` rows to postgres, remembering
   per partition how many vectors were fetched from the tablet and how many were consumed by
   postgres.

Per-query metrics record the fetch, collect and reduce phases. If the partition list version
changes between rounds (tablet split), the query is transparently restarted when nothing was
returned yet, or fails with `TryAgain` (retried by the upper layers) when results were already
consumed.

### Read paging

`LIMIT k` alone does not require paging: k is propagated to the tablets, and one round trip could
return the final answer. The situation changes when the query contains conditions that cannot be
pushed down to the index - for instance a filter that involves a join on another table. Final
filtering is then done by postgres, so there is no way to predict how many vectors must be
fetched from the index for k of them to survive the filter; postgres just keeps pulling until it
has k matches (or the index is exhausted). Deep paging is implemented without any tablet-side
cursor state:

- `PgClientSession` generates a UUID when it creates the `VectorIndexQuery`. When more data may
  exist, the response to postgres carries a paging state keyed by that UUID; postgres sends the
  paging state back verbatim with the follow-up request, and the matching UUID routes it to the
  still-live `VectorIndexQuery` in the session.
- All vectors fetched from the tablets but not yet returned to postgres are retained in the
  session, with per-tablet accounting of how many vectors were fetched and how many were already
  returned. The next page is filled from this pool first.
- A tablet is asked for its next portion only when it can no longer guarantee a full page by
  itself: fewer than `prefetch_size` of its vectors remain unreturned, and it has not yet
  reported that all its vectors were fetched. The second page therefore nearly always causes
  extra index lookups: every tablet that contributed at least one vector to the first page falls
  below this guarantee. After that the scheme settles: each portion adds `prefetch_size`
  unreturned vectors to its tablet while a page consumes `prefetch_size` in total across all
  tablets, so with evenly distributed results a tablet is re-queried roughly once per N pages
  (for N tablets) and the pages in between are served from the pool without touching the tablets
  at all.
- A vector index has no effective way to pull the "second top-k" of a finished search. So when a
  tablet is asked for its next portion, it re-runs the whole search with the target of
  `fetched + prefetch_size` results and drops the first `fetched` of them - the
  `num_top_vectors_to_remove` mechanism described in section 12.

Re-running the search per portion trades tablet-side statelessness for repeated work; the
`(distance, vector_id)` total order makes the drop-prefix scheme deterministic.

## 14. Backfill

Rows written before index creation are loaded by a tablet-local backfill, not by the generic
master-driven PgsqlRead backfill used for ordinary secondary indexes. The master still drives the
standard index permission state machine (`WRITE_AND_DELETE -> DO_BACKFILL ->
READ_WRITE_AND_DELETE`); what differs is the executor.

`TabletVectorIndexes::Backfill`:

1. Scans the indexed table with a projection of just the vector column, at the backfill hybrid
   time (all newer writes are covered by insert-on-apply, thanks to the creation-time gate).
2. Sizes chunks by memory budget: `--vector_index_backfill_single_chunk_size_bytes` (default
   1 GB) is converted into a vector count via the backend's `EstimateNumVectorsForBytes`,
   otherwise `--vector_index_initial_chunk_size` rows per chunk.
3. After each chunk it flushes the LSM, recording the resume position in the flushed frontier
   (`backfill_key` = next ybctid). The final chunk's frontier carries `backfill_done` instead.
4. Reverse mapping entries are written by a direct writer to the regular DB, unless the table
   owns its reverse mapping (then they already exist).
5. On completion the regular DB and the index are flushed.

Because the resume position lives in the LSM's own flushed frontier, an interrupted backfill
(restart, leader change with remote bootstrap) resumes from the last durable chunk boundary. On
tablet open, backfill is re-launched for any index whose frontier lacks `backfill_done`.

Completion is reported to the master through tablet heartbeats
(`TabletStatusPB.vector_index_finished_backfills`); the master advances index permissions when
all tablets report done, and postgres unblocks `CREATE INDEX` via `YBCPgWaitVectorIndexReady`
polling. Reads reject an index whose backfill has not finished, so a partially built index is
never queried.

## 15. Tablet lifecycle integration

### Frontiers and flush coordination

The Vector LSM exposes the same frontier interface as the tablet's RocksDB instances:
`GetFlushedFrontier` returns the largest `ConsensusFrontier` recorded in the manifest, and
`GetFlushAbility` reports no-new-data / already-flushing / has-new-data. The tablet's flush
machinery (`FlushFlags::kVectorIndexes`) and `FillMaxPersistentOpIds` treat every vector index as
an additional storage next to the regular and intents DBs. The consensus frontier itself carries
three vector-specific fields: `backfill_done`, `backfill_key` and `has_vector_deletion`.

### Intents DB flush prevention

An applied transactional write exists in three places: the intents DB (provisional record), the
regular DB (applied record) and, for vector columns, the mutable chunk of the vector LSM. Tablet
bootstrap replays an APPLY operation only if the intents DB has not yet flushed past it; once the
intents memtable containing the apply is flushed, replay will skip it. If that happened while the
corresponding vectors were still only in the (volatile) mutable chunk, a crash would lose them.

`Tablet::IntentsDbFlushFilter` therefore blocks flushing an intents memtable until both the
regular DB and every vector index have flushed frontiers at or beyond the memtable's largest op
index. The same filter already ordered intents behind the regular DB; vector indexes extend the
check to all vector storages. If the blocking storages lag for longer than
`--intents_flush_max_delay_ms` (or shutdown/write-stall), the filter force-flushes them so the
intents flush can proceed.

### Tablet bootstrap

On open, the tablet loads every vector index (manifest replay, parallel chunk load) before WAL
replay, and collects per-index flushed OpIds. For each replayed operation the bootstrap computes
a `StorageSet`: the set of storages (regular DB bit plus one bit per vector index) whose flushed
frontier is behind the operation. The write path consults this set, so a replayed apply
re-inserts vectors only into indexes that do not already have them durably. Without this gating,
a vector durable in a chunk would be inserted a second time on restart (same `VectorId`, so
search dedup would hide it, but the graph would hold garbage).

### Checkpoints, remote bootstrap, backup and restore

Tablet checkpoints (`TabletSnapshots::DoCreateCheckpoint`) checkpoint the RocksDB instances first
and then call `VectorLSM::CreateCheckpoint` per index, which hard-links the immutable chunk files
and manifest. Ordering matters: the vector checkpoint must not be older than the regular DB
checkpoint it will be restored with. Remote bootstrap enumerates the checkpoint directory
recursively, so chunk files and manifests ship with the rest of the tablet data. Backup/restore
and clone preserve the index options `id` (hence the storage directory name) and the
`owns_vector_reverse_mapping` table property.

### Shutdown

Vector indexes are shut down before the RocksDB instances, since search and compaction read the
reverse mapping from the regular DB. Shutdown drains the insert registry, waits for chunks in the
save queue to reach the manifest, cancels queued compaction tasks and waits for running ones.

## 16. Tablet splitting

Splitting interacts with vector indexes in three ways:

- Gating. Tablets of the vector index cotable are never split directly. Splitting a base table
  that has a vector index is controlled by the `--enable_tablet_split_of_tables_with_vector_index`
  auto-flag; manual splits bypass this check. Splits are also refused while any vector index
  backfill is active on the tablet (the heartbeat carries an active-backfill flag).
- Mechanism. A split creates both children from a checkpoint of the parent, so each child starts
  with a hard-linked copy of the full, unpartitioned vector index. There is no key-range
  partitioning of HNSW chunks. Out-of-range vectors are invisible to searches immediately
  (their reverse mappings fall outside the child's key range, so the read-time filter and ybctid
  resolution drop them) and are physically removed by the post-split full compaction, which
  includes vector indexes when `--vector_index_include_into_post_split_compaction` (default true)
  and applies the merge filter.
- In-flight queries. A `VectorIndexQuery` detects the partition list version change; if it has
  already returned rows to postgres it fails with `TryAgain` (statement-level retry), otherwise
  it refreshes the partition list and re-fans out transparently.

## 17. Metrics

Per-index (tablet) metrics, from the Vector LSM and the DocDB adapter:

- Flush: `flush_write_bytes`, `flush_us`.
- Compaction: `compact_read_bytes`, `compact_write_bytes`, `compact_us`.
- Search: chunk count, entries found, per-phase timings (`chunks_search_us`,
  `insert_registry_search_us`, insert registry entries).
- Read execution: data/intents read timings, merge time, found intents, result size, filter
  counters.

Session-level (fan-out) metrics: `vector_index_fetch_us`, `vector_index_collect_us`,
`vector_index_reduce_us` for the scatter, gather and merge phases of `VectorIndexQuery`.

Block cache (server-level): `vector_index_cache_query` / `_hit`, `vector_index_read` (bytes read
from disk), `vector_index_cache_add` / `_evict` / `_remove` (bytes), `vector_index_cache_read_us`,
`vector_index_cache_take_wait_us` (time spent waiting for a concurrent load of the same block).

Diagnostics: `--vector_index_dump_stats` logs per-search and per-fan-out statistics; searches are
visible in ASH under the `VectorIndex_Search` wait state.

Known issue: `take_wait_us` is currently instantiated from the `read_us` metric prototype, so the
two report under the same name (`hnsw_block_cache.cc`).

## 18. Memory tracking

Several mechanisms bound and account vector index memory:

- In-memory graph accounting. Each chunk backend reports its memory through
  `IndexMemoryConsumption`, which splits a parent MemTracker into `index_data` (graph and vector
  storage; grows on reserve/insert/load) and `search_contexts` (per-thread search scratch). Both
  are monotonic, mirroring the fact that usearch/hnswlib never shrink until destruction.
- Block cache reservation. In-memory (mutable and not-yet-converted) chunks consume space from
  the shared RocksDB block cache via `Cache::ConsumeSpace` during `Reserve`
  (`--vector_index_enable_block_cache_reservation`, default true). This shrinks the cache's
  effective capacity so that RocksDB blocks are evicted instead of total memory exceeding the
  configured budget.
- yb_hnsw paging. Served chunk blocks are charged directly to the same RocksDB cache
  (multi-touch), with per-block bookkeeping structures charged to the
  `vector_index_block_cache` MemTracker. Eviction pressure is shared with RocksDB block caching.
- Backfill budgeting. Backfill sizes chunks from a byte budget
  (`--vector_index_backfill_single_chunk_size_bytes`) using the backend's
  `EstimateNumVectorsForBytes`, bounding peak build memory.

Known gap: each hnswlib chunk owns its own `VisitedListPool` sized for its capacity, so scratch
memory duplicates across chunks (TODO in `hnswlib_wrapper.cc`).

## 19. Flags reference

All gflags related to vector indexes, grouped by area. Runtime unless noted otherwise. Index
reloptions (`m`, `m0`, `ef_construction`) and the `hnsw.ef_search` GUC are covered in section 3.

### Cluster-level and DDL

| Flag | Default | Meaning |
|---|---|---|
| `ysql_yb_enable_docdb_vector_type` | auto: false->true | allow `vector` columns (PG flag) |
| `vector_index_backend` | `yb_hnsw_hnswlib` | backend stamped into new indexes (master) |
| `enable_table_owned_vector_reverse_mapping` | false | new tables write their own reverse mapping |
| `enable_tablet_split_of_tables_with_vector_index` | auto: false->true | allow base table splits |

### Write path and insert concurrency

| Flag | Default | Meaning |
|---|---|---|
| `vector_index_initial_chunk_size` | 100000 | max vectors in the mutable chunk |
| `vector_index_task_size` | 16 | vectors per insert subtask |
| `vector_index_max_insert_tasks` | 1000 | insert back-pressure limit |
| `vector_index_task_pool_size` | 1000 | reusable insert task objects |
| `vector_index_max_merge_tasks` | 100 | merge subtask cap (0 = single-threaded merge) |
| `vector_index_concurrent_writes` | 0 = CPUs | insert thread pool size (non-runtime) |
| `vector_index_concurrent_reads` | 0 = CPUs | per-chunk search concurrency (non-runtime) |

### Read path

| Flag | Default | Meaning |
|---|---|---|
| `vector_index_skip_filter_check` | false | never build the read-time filter |
| `vector_index_no_deletions_skip_filter_check` | true | skip filter if no deletions, no WHERE |
| `vector_index_dump_stats` | false | log per-search and fan-out statistics |

### Backfill

| Flag | Default | Meaning |
|---|---|---|
| `vector_index_backfill_single_chunk_size_bytes` | 1 GB | memory budget per backfill chunk |

### Compaction

| Flag | Default | Meaning |
|---|---|---|
| `vector_index_enable_compactions` | true | enable background compactions |
| `vector_index_files_number_compaction_trigger` | 5 | min chunks to trigger compaction |
| `vector_index_compaction_size_amp_max_percent` | 200 | size amplification threshold (-1 off) |
| `vector_index_compaction_size_amp_max_merge_width` | 0 | max inputs for size-amp merge (0 = inf) |
| `vector_index_compaction_size_ratio_percent` | 20 | size ratio threshold (<= -100 off) |
| `vector_index_compaction_size_ratio_min_merge_width` | 4 | min inputs for size-ratio merge |
| `vector_index_compaction_size_ratio_max_merge_width` | 0 | max inputs for size-ratio merge |
| `vector_index_compaction_always_include_size_threshold` | 64 MB | always merge chunks below this |
| `vector_index_compaction_chunk_max_mem_store_size_mb` | 0 | output chunk split size (0 = off) |
| `vector_index_compaction_priority_start_bound` | 0 | priority pool tuning (-1 = global) |
| `vector_index_compaction_priority_step_size` | -1 | priority pool tuning (-1 = global) |
| `vector_index_num_compactions_limit` | 1 | concurrent compactions per tserver (0 = off) |
| `vector_index_include_into_post_split_compaction` | true | compact indexes after tablet split |

### Memory and block cache

| Flag | Default | Meaning |
|---|---|---|
| `vector_index_enable_block_cache_reservation` | true | charge in-memory chunks to block cache |
| `yb_hnsw_max_block_size` | 64 KB | max yb_hnsw block size at conversion |
| `yb_hnsw_keep_new_blocks_in_cache` | false | keep just-written blocks resident |

Tooling: `ts-cli` accepts `exclude_vector_indexes` to skip vector indexes when triggering manual
tablet compactions. Test-only flags (`TEST_vector_index_*`, backfill sleep/injection flags) are
not listed here.

## 20. Future work and known gaps

- Multicolumn vector indexes (`amcanmulticol`), partial vector indexes (need predicate pushdown
  to the access method), index-only scans for copartitioned indexes.
- Opclasses for `halfvec` / `sparsevec` / quantized vectors; `PgVectorIndexType::IVFFLAT` is
  reserved in the protocol but not implemented.
- A real cost model (`ybvectorcostestimate` is generic), so plans do not depend on hints.
- Large transactions on tables with vector indexes.
- Split-native index handling: children currently inherit the full graph and rely on post-split
  compaction; range-partitioning chunks at split time would avoid the transient duplication.
- Session-layer handling of splits under deep paging without statement retry.
- Per-tablet paging state to avoid re-running the ANN search with growing `max_results` on deep
  pagination.
