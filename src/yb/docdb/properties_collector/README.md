# DocDB SST properties collectors

DocDB-aware `rocksdb::TablePropertiesCollector` implementations: code that observes every entry
while an SST file is built (on flush and on compaction) and stores what it learned in the file's
own properties block. The first collector, the **SST statistics collector**, measures garbage
(tombstones, shadowed versions, dead rows) and its shape (chain lengths, stretches, ages); its code
arrives in the changes stacked on this directory, and this README describes the component as
designed. The directory is expected to grow other collectors.

This file holds what does not fit as a comment on one class or function: why the component exists,
the vocabulary the files share, and the design choices that cut across them. Properties, flags and
the per-entry algorithm are documented where they are defined.

## Why

DocDB deletes and overwrites do not remove data; they add entries (a tombstone marker, a newer
version) that shadow older ones. The shadowed data is reclaimed only when a compaction rewrites the
file after history retention has passed. Until then reads pay for it: a scan steps over every
tombstone and stale version in its range.

The only signal that previously triggered a full compaction for this reason was read-driven: the
`FullCompactionManager` watches per-tablet counters that the read path increments when it steps
over obsolete keys. That signal is blind in two structural ways. It exists only where reads happen,
so followers (which serve no reads) and point-get workloads (which step over nothing) never report;
and it is post hoc, the first read to suffer is the first to report.

The collector measures garbage where it is written instead: every entry passes through one place
during an SST build, on every replica, whether or not anyone ever reads it.

## The hook

`BlockBasedTableBuilder::Add` calls every registered `TablePropertiesCollector` once per entry, and
`Finish` when the file is complete; what the collector returns is written into the file's
`user_collected_properties`. `Finish` is NOT called exactly once: `GetTableProperties()` invokes it
again, under `CHECK_OK`, so a collector's `Finish` must be idempotent and must never return non-OK
(a failure there aborts the process, not just the build). This is stock RocksDB: it runs an
internal collector on every build,
and upstream ships a `CompactOnDeletionCollector` on the same hook. That one is unusable here: it
classifies native RocksDB deletion records, which DocDB never writes (a DocDB delete is a `Put`
whose value is a tombstone marker); it stores nothing in the file, only a `NeedCompact()` boolean;
and that boolean feeds the level-style compaction picker, which DocDB does not use.

The collector is registered per tablet on the **regular** RocksDB only (`Tablet::OpenRegularDB`),
behind a flag, and lives for exactly one SST build. A collector must never fail a build, so parse
failures are recorded (`chain_valid`), never returned.

## Vocabulary

DocDB stores one logical row as many key-value entries, each stamped with a hybrid time: ideally one
packed row holding all columns, plus per-column updates. The file is sorted so that all versions of
a key are adjacent, newest first. Two chain units:

- A **subdoc chain** is the run of consecutive entries sharing one subdocument key: the versions of
  one record (the packed row, or one column of one row). Its first entry is the live head.
- A **row chain** is the run of consecutive entries sharing one row key (the DocKey, including the
  cotable / colocation prefix).

Three counters over the chain-tracked entries of a file give the classification for free:
`Ec` = chain-tracked entries, `K` = distinct subdoc keys, `R` = distinct rows.

- **shadowed** = `Ec - K`: entries that are not the newest version of their key. Garbage.
- **repackable** = `K - R`: heads beyond one per row; a measure of what a repack would fold into
  one packed row, not a garbage class. In a live row these heads are live data; in a dead row they
  are also garbage (counted by reclaimable below), so repackable and reclaimable overlap there.
  Never a trigger input either way.
- **collapsible** = `Ec - R` = shadowed + repackable. Mixed. Never a trigger input.

Recognized at scan time:

- A **covering write** is an entry at the row key itself: a packed row or a row-level (or
  table-level) tombstone. It overwrites the whole row, so every entry of the row with an older
  hybrid time is garbage even when it is the newest version of its own record. File order is key
  order, not time order -- a covering write sits at the front of its row but may be newer or older
  than any given entry behind it, so each record head is compared against the row's covering
  writes by hybrid time.
- A **dead row** is a row whose newest covering write is a tombstone with nothing newer in the
  row. A row re-inserted after a delete is live again, tombstone notwithstanding.
- **reclaimable** = shadowed versions + record heads older than a covering write + tombstone
  markers themselves (a tombstone past the history cutoff drops at a full compaction whether or
  not the row lives on). All the garbage, regardless of whether retention allows removing it yet,
  counted once online.
- **droppable** = the part of reclaimable already past the history cutoff a consumer applies. The
  compaction trigger uses droppable; the metric reports reclaimable.
- A **stretch** is a maximal run of consecutive reclaimable entries in file order, ignoring key and
  row boundaries. Chain length measures per-row depth; stretch length measures what a cursor read
  must step over before reaching live data.

Worked example (the anatomy strip used in the tests). Live row r1 with six entries, newest first
within each key, then dead row r2 with three:

```
r1: [packed v3][packed v2][packed v1][col a v2][col a v1][col b v1]
     live head   shadowed   shadowed   covered   shadowed  head
                                       (older than packed v3)
r2: [row tombstone][col a v1][col b v1]   <- both columns older than the tombstone: row is dead

Ec = 9, K = 6, R = 2
shadowed    = 3      repackable = 4 (r1.a, r1.b + r2.a, r2.b -- an identity over
                                     heads; three of these four are also garbage)  collapsible = 7
dead rows   = 1      dead-row entries = 3
reclaimable = 3 (shadowed) + 1 (r1.a covered by packed v3) + 3 (all of r2) = 7
stretches   = [packed v2 .. col a v1] = 4, [r2 x 3] = 3
```
(`col b v1` shares packed v3's hybrid time, so no covering write is newer than it: live.)

Why both distributions: a range of rows that were all deleted has short row chains but one very long
stretch; every other row deleted has short stretches but half the file reclaimable.

Shadowed, repackable and collapsible are never stored: they are identities over the stored counters,
valid only over chain-tracked entries (meta records excluded) and only while `chain_valid` is set.

## Histogram layout

The distributions use one fixed 145-bucket layout (`ExponentialHistogram`): values 1..16 exact,
then 8 equal sub-buckets per power-of-two range up to 2^20, then one overflow bucket. This is
HdrHistogram's **log-linear** layout: the same bucket count as the exponential family at scale 3,
but boundaries linear within each power-of-two range (worst-case bucket width 12.5%, vs a uniform
9.05% for true-geometric `2^(k/8)` edges), which is what makes the index one bit-scan plus a shift
and a mask, no floating point. Quantiles read from bucket bounds are within 12.5%, except in the
overflow bucket, where the error is unbounded.

Merging is bucket-wise addition, exact at every rollup: file -> tablet -> table -> fleet.
Coarsening is halving the sub-bucket count within the linear family (summing adjacent pairs), also
exact; the serialized layout tag is what keeps files from different generations mergeable if the
sub-bucket count ever changes. The tag deliberately does not claim the OTel/Prometheus geometric
convention, because these are not those boundaries.

Why a full histogram rather than a few percentiles: percentiles of files do not compose into the
percentile of a tablet, and the questions we will ask later ("bytes in chains longer than 64?",
"p99.9?") are not known when the file is written. Percentiles would save a few hundred bytes per
file.

Why not reuse an existing class: `yb::HdrHistogram` is built for concurrent latency recording and
does five lock-prefixed read-modify-writes plus two CAS loops per `Add`, on a path that runs once
per row; it also lacks a merge operation. `rocksdb::HistogramImpl` merges, but its bucket table is
one process-global constant shared by every RocksDB latency histogram, so it can be neither given
this layout nor coarsened exactly, and each `Add` walks a `std::map`.

## Age bands and "droppable"

A reclaimable entry can be removed by a compaction only once the write that makes it garbage is at
or below the history cutoff (the compaction feed's overwrite-stack rule): the OLDEST such write --
the entry's own successor version or a covering write, whichever is older -- and for a tombstone
marker its own hybrid time. The collector buckets every reclaimable entry by the **age of that
write**, relative to a wall-clock anchor taken at
collector construction, into eight fixed bands whose edges fall on retention values in use (5 m,
15 m, 1 h, 6 h, 24 h, 7 d, 30 d; the 15 m edge brackets the 900 s default). A consumer applying
cutoff `C` sums the bands wholly older than `anchor - C`; the straddling band is excluded, so the
estimate is conservative by at most one band.

Why bands rather than "is the whole file past the cutoff": compaction outputs span hours to days
and always contain a sliver of recent data, so a file-level test would count none of their garbage
as droppable even when most of it is weeks old, and those are the files where debt lives. Why bands
rather than a build-time bit: the cutoff moves with wall-clock time, retention configuration, and
holds (CDC, xCluster, PITR schedules) that appear and disappear.

Both entries and bytes are banded. A trigger should ratio **bytes**: an entry ratio over-fires on
tiny tombstones over large live rows (a million tombstones above a million 1 KB rows is half the
entries and one percent of the bytes), and a byte ratio bounds the write amplification of a
triggered compaction to `1 / threshold`.

## Cost

The build path already walks doc-key components for every entry (the bloom-filter key transform).
The tracker adds one word-wise shared-prefix compare against the previous key, one short
encoded-hybrid-time compare against the row's covering writes (encoded order is time order, so no
decode), a few counter increments, and, per garbage entry, one hybrid-time decode for the band.
Per row: one `DocKey::EncodedSize` walk, up to a handful of covering-write decodes, and two
histogram increments. No allocation in steady state, no atomics, no floating point. The acceptance
bar is end-to-end flush and compaction throughput with the flag on versus off.

## Boundaries

This component only produces the per-file record. Its consumers are separate: a per-tablet
in-memory aggregate fed by the RocksDB listener (`TableProperties::Add` does not merge
`user_collected_properties`, so aggregation goes through `SstStatsFromProperties`), the
`docdb_sst_*` Prometheus gauges for humans (additive scalars only; this metrics system exports no
bucket vectors), and a full-compaction trigger clause that reads the aggregate directly. Every
consumer must account for **coverage**: files that predate the collector carry no statistics, and a
ratio over them silently reads near zero.

Nothing here changes what a compaction removes; the compaction feed decides. Non-full compactions
already remove shadowed versions whose overwriter is in the same compaction; tombstones and dead-row
entries go only when no older data can exist outside the compaction, i.e. in a full compaction.
Sums across files are exact lower bounds on tablet-level collapsible entries (a row split across
files is under-counted, never over-counted); cross-file deduplication is out of scope.
