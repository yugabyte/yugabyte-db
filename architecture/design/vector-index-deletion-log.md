# Vector deletion log

Status: in development, #37982. The feature lands in parts; each section below names the part that brings it, and whether that part is done.

| Part | Content | Status |
|---|---|---|
| M0 | `VectorLSMDeletionMap`, the in-memory map | Done |
| M | The deletions in the indexes of the in-memory chunks, `VectorLSM::DeletionsLookup` | Done |
| A1 | The deletion blocks of the YbHnsw file | Done |
| S | A chunk save writes its deletions, a compaction keeps the deletions of its inputs | Planned |
| B2 | A compaction drops the deleted vectors, and the deletions it no longer needs | Planned |
| D | The write time of a vector in its payload | Planned |
| E | Restore marks for snapshots and restores to a time | Planned |
| C | DocDB produces the deletions, after the fix of #36288 | Planned |
| F | Search decides candidates by their write and deletion times | Planned |

Until S, a chunk save drops its deletions. Until B2, a compaction keeps every vector. Nothing produces deletions until C, so neither has a runtime effect.

## Motivation

A table created with `vector_index_store_payload` writes no vector reverse mapping: each vector carries the ybctid of its row in its payload, so search resolves rows without the mapping. Compaction of a vector index of a table with the reverse mapping drops a vector once its mapping entry is gone. Without the mapping, compaction has nothing to check a vector against, so the index grows with the number of vector updates and deletes rather than rows. Search has nothing to check a candidate against either, so once the tablet has a vector deletion it reads the row of every candidate, to reject the deleted and replaced vectors.

The deletion log gives such an index the deletions of its vectors, so compaction can drop the deleted ones, and search decides a candidate by its deletion and the time it was written at, without reading its row. An index of a table with the reverse mapping keeps no log; everything below applies to the indexes of tables without it.

## Producing deletions

Part C, planned.

`PgsqlWriteOperation::FillRemovedVectorId` reads the old vector id of a row a DELETE or UPDATE removes or replaces, and sends it in `delete_vector_ids` for every table. For a table with the reverse mapping the ids become mapping tombstones, as before. For a table without it they become mapping tombstones with nothing behind them, which regular compactions drop, and deletions in every vector index of the tablet without the reverse mapping (`VectorIndexesUpdater::DeleteVectors`): the list does not say which table or column the ids are from.

A deletion is the pair (vector id, hybrid time): the vector is not visible at that time and later. The time is the one the row removal is stored at:

- the commit time, when the intents of a transaction are applied;
- the batch hybrid time, for a non-transactional write;
- the local batch hybrid time on an xCluster target, for both of its paths. The source time could be below the cutoff of a past compaction of the target, see below.

A DELETE or UPDATE rolled back to a savepoint must produce no deletion, see #36288. This feature has to land after that fix: compaction would otherwise drop the vector of a live row.

## Storage

The deletions are kept with the chunks of `VectorLSM`, so they share the flushed frontier, WAL replay, checkpoints, remote bootstrap and the hard links of a split with the vectors.

### In memory

Parts M0 and M, done.

`VectorLSM::Insert` takes deletions next to vectors (`VectorLSMInsertContext::deletions`). The index of the mutable chunk keeps them, as it keeps payloads: while it is mutable, the index wrapper holds them in a `VectorLSMDeletionMap`, which it creates with the first deletion. So every chunk answers lookups of its deletions (`VectorDeletionsReaderIf`) the same way, whether it is mutable, being saved or saved, and a chunk which received only deletions hands its index over to be saved too. A deletion is inserted without `VectorLSM::mutex_`, holding a task of the chunk, so the chunk is saved only after the insert.

The map is split into 256 shards by a multiplicative hash of the vector id, each a flat hash map behind an `rw_spinlock`, since a search looks up every candidate while more deletions are inserted. A vector is deleted only once, so the map ignores a repeated deletion, and a lookup stops at the first chunk with the deletion.

Deletions count toward the capacity of the chunk, weighted by the average memory of a deletion in the map against the memory of a vector in the index. So a chunk which receives only deletions rolls too, and a chunk with many deletions takes fewer vectors. A chunk with deletions has new data: it is flushed, and the flushed frontier does not pass it.

The `deletions` MemTracker under the tracker of the index tracks the maps.

`VectorLSM::DeletionsLookup` finds a deletion in the chunks that have deletions.

### On disk

Part A1 for the format, done; S for the save, planned.

A chunk save writes the deletions of the chunk into its YbHnsw file, after the blocks of the graph. A chunk which received only deletions gets a YbHnsw file without vectors. Footer flag `0x08` marks a file with deletions. Vector ids are random UUIDs (`VectorId::GenerateRandom`), so their first bits address the blocks, and no index of the blocks is kept:

- Data blocks: the first `deletion_bits` bits of the vector id give the block, `2^deletion_bits` blocks in all. The writer picks the number of bits from the number of deletions, so a block takes about `yb_hnsw_max_block_size`: about 2.7K deletions at 64 KB, 12 bits for 10M deletions. A block holds its deletions sorted by vector id, each the vector id without the bytes the block implies, followed by the 8-byte hybrid time, and is binary searched. Its size, which the footer has for every block, gives the number of entries.
- Filter blocks: the fixed-size bloom filters of the regular DB (`rocksdb::NewFixedSizeFilterPolicy`: 64 KB, 1% false positives, about 55K deletions each), addressed the same way by the first `deletion_filter_bits` bits, since nearly every lookup misses.
- A lookup reads the filter block of the vector id, and only when the filter may match, its data block.
- The blocks are read through the YbHnsw block cache, which bounds and tracks their memory. Vector ids that are not random only make blocks larger, or leave some empty.

### Manifest

Part B2, planned.

`VectorLSMChunkPB` records three hybrid times used by compaction: `min_vector_hybrid_time`, the smallest time a vector of the chunk was written at; `deletions_applied_hybrid_time`, the cutoff at or below which the compaction that wrote the chunk removed deleted vectors from it; and `history_cutoff`, see below. 0 means unknown.

## Compaction

Part S keeps every deletion of the input chunks in the output; part B2 adds the rest, planned.

The deletions a compaction keeps come out of its inputs sorted by vector id, unlike the inserted ones. The output index takes them in that form, without the hash map of the inserted deletions.

### The cutoff

A compaction removes the vectors deleted at or below its cutoff, `IndexContext::DeletionsCutoff`: the history cutoff of the tablet, kept at or below `Tablet::SafeTimeBelowRunningTransactions`, the safe time below the start of every transaction not applied yet. So every deletion at or below the cutoff, and every vector write at or below it, is already in the index. It is `kMin`, which removes nothing:

- until the transactions of the tablet are loaded;
- while the index is being backfilled, since a backfill inserts the vectors of the rows it read later than their deletions.

Only the deletions saved with their chunks count. The deletions not saved yet could still be lost from the WAL, see `wait_frontier_durable`, so the cutoff stays below the earliest of them.

### Removing vectors

A vector of an input chunk is dropped when the saved deletions of any chunk have its deletion at or below the cutoff.

### Keeping deletions

A deletion (V, T) of an input chunk is written to the output chunks while it can still matter:

- T is above the cutoff, so the vector stays and the deletion is needed later;
- or another chunk could still hold V: a chunk which has a vector written at or below T (`min_vector_hybrid_time <= T`) and from which no compaction removed the deletions at T (`deletions_applied_hybrid_time < T`). The mutable chunk counts as such a chunk while it has vectors.

Otherwise the deletion is dropped with its vector, or after it.

### Reads below the cutoff

A search at a time below the cutoff of a past compaction would miss the vectors the compaction removed, while their rows are visible at that time. The retention policy refuses to register such reads, but it keeps the cutoff only in memory, and the regular DB frontier does not have the cutoffs of vector index compactions. So every chunk records `history_cutoff`, the highest cutoff of the compactions behind it, which a later compaction with a lower cutoff does not lower. When a vector index opens its storage, it seeds the retention policy of the tablet with it (`TabletVectorIndexes::SeedHistoryCutoff`), after releasing `vector_indexes_mutex_`, since the retention policy takes that mutex under its own.

## Search

Part D for the write time and F for the search, planned.

The payload of every vector of such an index stores, next to the ybctid, its write time W: the hybrid time at or after which its row is visible. It is the commit time or the batch hybrid time of the write, and the backfill time for a backfilled vector.

`PgsqlVectorFilter` takes the deletions of the index (`DocVectorIndex::DeletionsLookup`) after reading the intents and before the search: an applied deletion is in the index before its intents are removed, and a compaction drops a deletion only together with its vector. Rows with intents are still decided by the intent row path. Any other candidate is visible at the read time R when W <= R and it has no deletion at or below R (`DecideByTimes`). A time is compared with the read limits as a row read would:

- in (R, local limit]: the read restarts;
- in (local limit, global limit]: the row is read. A row read restarts there for a transaction which wrote its intents at or below the local limit, and the payload has only the commit time;
- above the global limit: the write or deletion is not visible.

The row is also read when the request has a filter of its own, and for every candidate while `vector_index_no_deletions_skip_filter_check` is false. The vector deletion mark of the tablet does not make such a search set up row reads, but a tablet without the mark still searches with no filter at all, unless the table is colocated. `vector_index_filter_read_rows` counts the rows the filter reads.

An in-place truncate of a colocated table, with `yb_enable_alter_table_rewrite` off, tombstones the table without vector deletions. So the search also rejects the vectors written at or below the table tombstone time, taken from the tombstone cache of the table, and reads the rows until that time is cached or while `enable_colocated_table_tombstone_cache` is off.

## Snapshots and restores

Part E, planned.

The vector index storage of a transactional snapshot, and the storage a restore to a time uses, are from a later point than the regular DB, which is cut at the snapshot or restore time. `VectorLSMRestoreToTime` brings the storage to that time, at snapshot creation and at restore, without touching the chunk files, which can be hard links: it sets `restore_hybrid_time` of every chunk with a file, in a new manifest file, as the regular DB has a hybrid time filter for its files.

- Lookups ignore the deletions of the chunk above that time: the rows of the vectors they deleted are back. A compaction drops them from its output.
- The vectors of the chunk written after it are dead, since their rows are rolled back. Search skips them and compaction removes them. This assumes that the write time of a vector is not above the hybrid time of its row.

`TabletSnapshots::AllowedHistoryCutoff` keeps the history cutoff at or below the times of the scheduled snapshots, as it does for the regular DB, so no compaction removed a vector deleted after such a time.

## Rejected alternatives

- **Deletions kept by `VectorLSM` next to the index of a chunk**, a map on the mutable chunk handed to the immutable chunk until its save, and passed to `SaveToFile`. Lookups and compaction would then tell apart three places a deletion can be in. Kept in the index, as its payloads are, every chunk answers the same way, and `SaveToFile` keeps its signature.
- **A deletions file of its own per chunk**, sorted with one bloom filter over it, loaded and kept whole. 10M deletions take about 252 MB of memory that no memory tracker sees, and about 1 GB while their chunk is saved. The blocks of the YbHnsw file are read through its block cache instead, and share its checksums, versioning and file lifecycle.
- **A set of deletion files independent of the chunks**, merged on a schedule of its own. Lookups would probe fewer files, and deletions could be dropped without waiting for a compaction of the chunk that holds them. But dropping a deletion still needs the hybrid times of every chunk, and a second kind of compaction would update the manifest concurrently with the vector compactions, with its own frontier, checkpoint, split and remote bootstrap handling.
- **Reverse mapping tombstones in the regular DB.** Regular compaction drops a tombstone with nothing behind it: right away in a post-split compaction, below the history cutoff otherwise. Keeping them would make regular compaction depend on the watermarks of every vector index of the table, and the check would be a regular DB point read per vector.

## Known gaps

Of the design as a whole, once all parts land. Setting `vector_index_no_deletions_skip_filter_check` to false makes the search read the row of every candidate again, which works around the gaps of the search, but brings back no vector that a compaction or a restore mark removed.

- The delete list does not tell tables or columns apart, so an index without the reverse mapping also gets the deletions of the other vector columns of its table and, on a colocated tablet, of the other tables. They match none of its vectors, but take capacity of its mutable chunk, memory and space in its chunk files. The list needs scoping by table and column, through the intents and xCluster, before `vector_index_store_payload` is enabled by default.
- Vector indexes with xCluster are not supported yet. On a target the deletions are at local hybrid times, while the rows and the write times keep the source hybrid times, so a search, and a restore to a time, compare times of different clocks.
- An upsert, or the backfill insert of an online schema change, overwrites a row without reading it, so it deletes no vector.
- `yb-data-patcher` does not shift the write times and the deletion times.
- A compaction drops the table tombstone of an in-place truncate of a colocated table, after which a search accepts the vectors of the truncated rows.
- Backfilled vectors carry the hybrid time of the index, which is above their rows, so a snapshot below it, of a tablet that added the index before the snapshot, marks them dead. They are lost after a restore from it, and the index has to be rebuilt.
- The vector index check does not apply the visibility rules of the search.
- Vectors written before the write time was added have none, and a search accepts them without checking their deletions. During a rolling upgrade a replica on an older release records no deletions. Such indexes have to be recreated.
- Older releases fail to open a YbHnsw file with deletions, footer flag `0x08`, ignore `restore_hybrid_time`, and fail to decode a payload with a write time beyond its ybctid. `vector_index_store_payload` is experimental and off by default, so no AutoFlag gates the formats; one is needed before the flag is enabled by default.
