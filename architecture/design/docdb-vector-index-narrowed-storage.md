# Vector index narrowed coordinate storage

A DocDB vector index (`ybhnsw`) can store the coordinates of a served chunk in a narrower
encoding than `float32`: either `float16`, or `int8` paired with a `float16` rerank tier. The
HNSW graph is always built at full `float32` precision -- only the copies written into the
immutable chunk file are narrowed.

`float32` is the default and writes version-1 chunks, so an index that does not opt in is
unaffected.

## Record layout

`VectorStorageKind` names the traversal encoding, `RerankStorageKind` the optional second copy.
Per vector at 768 dimensions:

| encoding | traversal | rerank | record | vs `float32` | bytes per distance |
|---|---|---|---|---|---|
| `float32` | 3072 | -- | 3092 | 1.00x | 3072 |
| `float16` | 1536 | -- | 1556 | 0.50x | 1536 |
| `int8` | 768 | 1536 | 2324 | 0.75x | **768** |

The 20-byte overhead is the per-vector `YbHnswVectorData` header. A traversal touches thousands
of records per query while the rerank tier reads only the retained candidates, so `int8` spends
0.75x the disk to make the hot path read a quarter of the bytes.

`coordinate_codec.{h,cc}` owns both directions and nothing else narrows coordinates: writer and
query path must round identically, or a vector stops being at distance zero from itself and the
graph's neighbour choices stop agreeing with the distances used to search it.

## Measured

VectorDBBench `Performance768D1M` (Cohere, cosine), `k=100`, 30 clients, versus `float32` at the
same `ef_search`:

| setup | ef | encoding | recall delta | throughput |
|---|---|---|---|---|
| RF3 | 200 | `float16` | +0.06% | +12.5% |
| RF3 | 200 | `int8`, overfetch 1 | +0.01% | **+39.8%** |
| RF1 | 200 | `float16` | -0.02% | +20.6% |
| RF1 | 200 | `int8`, overfetch 2 | +0.01% | **+32.8%** |

Fitting `speedup = 1/((1-f) + f/c)` across eight recall-matched cells, `c` being the
coordinate-byte ratio, gives **f = 0.30 +/- 0.06**: only ~30% of end-to-end query time scales
with coordinate bytes. The ceiling for any coordinate encoding is therefore `1/(1-0.30)` =
**1.43x**, and `int8` reaches ~93% of it. The rest is tablet fan-out, resolving and fetching
`LIMIT` rows per query, neighbour-list reads that no encoding shrinks, and YSQL/RPC overhead.
This is why nothing narrower than `int8` is worth adding: `int4` would buy roughly 3%.

## Index size and memory footprint

The two encodings move footprint in opposite directions, which is the main reason to pick one over
the other. Measured file sizes for 1M vectors at 768 dimensions (from a single-node
`ef_search=200` run, not the RF1/RF3 runs above):

| encoding | record | vs `float32` | index size | records per fixed cache budget | bytes touched per visit |
|---|---|---|---|---|---|
| `float32` | 3092 | 1.00x | 3.19 GB | 1.00x | 3092 |
| `float16` | 1556 | 0.50x | **1.65 GB** | **1.99x** | 1556 |
| `int8` + rerank | 2324 | 0.75x | 2.42 GB | 1.33x | **788** |

`float16` is the capacity option: it halves the index and keeps ~2x as many vectors resident in the
same block cache, at identical recall. `int8` is the throughput option: its record is *larger*
than `float16`'s, because the rerank copy costs more than the narrowed traversal copy saves, so it
shrinks the index only 24% -- but a traversal reads just the header and traversal coordinates, so
what the hot path touches drops to **0.25x**. The rerank copy is read only for the retained
candidates.

Both file sizes follow from the record layout alone. 1M records account for 3.09 / 1.56 / 2.32 GB,
leaving ~0.10 GB of neighbour lists and aux entries that no encoding shrinks; adding that back
gives 1.65 GB and 2.42 GB, which is what was measured to the last digit. There is no second effect
to account for, and the ratios hold at any vector count where records dominate the file.

## The rerank tier, and why `ef` is not the knob

`int8` quantizes each coordinate to a per-chunk step, costing several points of recall@k.
**Raising `ef` does not recover it** -- a search retains `max_num_results` entries ranked by that
same quantized distance, so widening the candidate set does not improve the final selection.

What recovers it is retaining more candidates than requested and rescoring them at `float16`
before they leave the chunk. `MakeResult` is the only point at which distances cross the chunk
boundary, so it is also the only point at which they must be in the metric's own units for
VectorLSM to merge them against other chunks.

The candidate budget is `max(ef_search, LIMIT, factor * LIMIT)`. While `factor * LIMIT <=
ef_search` the over-fetch is free, since the search already retains that many candidates. Above
it the budget becomes `factor * LIMIT` and `ef_search` goes inert below that -- at the default
factor of 2 with `LIMIT 100`, an `ef_search` of 100 and of 200 run the identical search. Either
set the factor to 1 and let `ef_search` control depth, or raise `ef_search` past
`factor * LIMIT`. The RF3 numbers above use factor 1; RF1 needs factor 2, which costs it 1.1
recall points at factor 1, so this is a per-deployment choice.

## Per-chunk quantization scale

`int8` uses a symmetric scale, `max|coordinate| / 127`, derived from the chunk's own coordinates
and recorded in its footer. Scoping it to the chunk keeps it a pure function of data already in
hand -- no sampling, no configuration, nothing to recalibrate as an index grows -- at the cost
that two chunks of one index hold different scales. A query must therefore be quantized against
the scale of the chunk it is compared against, never a freshly computed one.

This is also why compaction reads the **rerank** copy rather than decoding the traversal
coordinates. A merged chunk derives a new scale, so decoding `int8` would feed already-quantized
values into a fresh quantization and lose a little more on every merge. The `float16` copy
round-trips exactly, making a vector a fixed point across arbitrarily many compactions.

## Index size and memory footprint

Halving the coordinate bytes halves what the index costs on disk and in the block cache. Measured
file sizes for 1M vectors at 768 dimensions (from a single-node `ef_search=200` run, not the
RF1/RF3 runs above):

| encoding | record | vs `float32` | index size | records per fixed cache budget |
|---|---|---|---|---|
| `float32` | 3092 | 1.00x | 3.19 GB | 1.00x |
| `float16` | 1556 | 0.50x | **1.65 GB** | **1.99x** |

The file ratio is 0.52x rather than 0.50x because the graph and aux data do not shrink. That is
the whole of the discrepancy: 1M records account for 3.09 GB at `float32` and 1.56 GB at
`float16`, leaving ~0.10 GB of neighbour lists and aux entries unchanged, and 1.56 + 0.10 = 1.65 GB
is what was measured. So the on-disk saving is predicted by the record layout alone -- there is no
second effect to account for.

The block cache holds records, so the same budget keeps ~2x as many vectors resident. That matters
more than the disk saving: at 1M x 768d a `float32` index does not fit a typical cache, and the
traversal is what pays for the misses.

## Chunk format

Each chunk records its own encoding in its footer, under a serialization version:

| version | contents |
|---|---|
| 1 | base layout; always `float32` |
| 2 | adds `Header::storage_kind` |
| 3 | adds `Header::rerank_kind` and `Header::quantization_scale` |

The writer emits the lowest version that can represent the header, which keeps a `float32` index
at version 1. `Load` rejects a version outside 1..3 rather than reading the fields it knows and
consuming the remainder as block offsets, and `ValidateHeader` then checks the parsed header
against itself before any record is read through it: a `vector_data_size` disagreeing with the
encodings would read past every record into the next, an unusable scale would decode coordinates
to zero or infinity, and an out-of-range encoding enum would reach `FATAL_INVALID_ENUM_VALUE` and
abort the process rather than failing the one file.

## Search hot path

Two changes are independent of which encoding an index uses.

**Neighbour prefetch** (`yb_hnsw_prefetch_neighbors`, default on). Visiting a neighbour costs two
dependent cache misses the CPU cannot speculate through: `blocks_[index]`, then the record it
points at. The loop filters a node's neighbours through the visited set, resolves all their
record addresses and prefetches each, before computing any distance, so the misses overlap. Both
loop shapes call one `visit` lambda in neighbour order, so results are identical --
`PrefetchMatchesSerialResolution` asserts that, including equal cache query and hit counts. The
visited filter must stay first, or resolving addresses for already-visited neighbours would
`Take()` blocks the search does not need. **Measured neutral** (see Caveats); the flag allows
A/B in one process.

**Batched cache counters.** `cache_query`/`cache_hit` are accumulated in `SearchCache` and
flushed once per search rather than incremented inside `CachedBlock::Take`, which a query calls
thousands of times. `Take` reports residency through a `was_hit` out-parameter; a caller passing
`nullptr` gets no accounting, as the header states.

## Enabling an encoding

A **master** flag chooses the encoding, stamped into the index's catalog entry at `CREATE INDEX`.
It is therefore fixed for the life of that index and identical on every replica, rather than
following each tserver's local flags.

```bash
# On every yb-master. Runtime-settable; no restart needed.
--vector_index_storage_coordinate_type=int8      # or float16, or float32 (default)

# On every yb-tserver. 1 disables the over-fetch. Valid range 1-64.
--vector_index_rerank_overfetch_factor=2         # default
```

An index keeps the encoding it was created with, so the flag's value at query time says nothing
about an index created earlier. To see what an index got, read `vector_data_size` from the index
flush log line: `20 + 4*d` for `float32`, `20 + 2*d` for `float16`, `20 + 3*d` for `int8`.

### Downgrade

Selecting a narrow encoding is one-way for the chunks written while it is selected. A version-2
or version-3 chunk is unreadable by a binary that only understands version 1. Clearing the flag
stops new chunks from using the encoding but does not rewrite existing ones, and an index created
while it was set keeps its encoding in the catalog. Moving such a cluster to a binary without
these encodings requires those indexes be dropped and rebuilt.

## SIMD kernels

x86-64 builds at `-march=ivybridge`, so no AVX-512 family enables itself from its `-march` macro
and `usearch_include_wrapper_internal.h` forces the three SimSIMD targets the encodings need:
`HASWELL`, `SKYLAKE` and `ICE`. Forcing a target is safe -- each kernel carries its own target
attribute so it compiles under the Ivy Bridge baseline, and `simsimd_capabilities()` dispatches
on CPUID, so a kernel the host cannot run is never called.

`ICE` is what the narrowed encodings depend on: SimSIMD has no `*_i8_skylake`, so without it the
`int8` ladder drops from AVX-512 straight to the 256-bit Haswell kernels while `float32` keeps a
512-bit one. `cos/l2sq/dot_i8_ice` accumulate into `int32` exactly as the Haswell kernels do, so
they are bit-identical and cost no accuracy. `float16` distances use the 256-bit `*_f16_haswell`
kernels, which widen each coordinate to fp32 and accumulate there -- which is why `float16` buys
less throughput than its byte count suggests.

The `#define`s reach only translation units including `usearch_include_wrapper_internal.h`,
directly or through `hnsw.h`: 5 production files, all vector-index code, plus 4 tests. `hnsw.h` is
included by no other header, so nothing in `tserver`, `master`, `rocksdb` or `postgres` is
reached. Vector indexes are YSQL-only, so none of this can reach a YCQL workload. `int32`
accumulator headroom for `int8` is 8.3x: saturation needs `d > 133,144` against a
`VECTOR_MAX_DIM` of 16,000.

## Caveats

- **No sanitizer coverage of the SIMD kernels.** SimSIMD is disabled under ASAN and TSAN, whose
  builds exercise usearch's scalar metric instead. The wide intrinsic loads largely evade
  instrumentation, so a bad read inside one faults or is missed rather than reported.
- **One workload.** Every number above is Cohere 768d/1M, `k=100`, 30 clients, read-only. The
  dispatch split, the 30% byte-proportional fraction, and the balance between bytes moved and
  kernel cost will differ on another host, at another dimension count, at a different `LIMIT`, or
  under a mixed read/write load.
- **Latency is unmeasured.** The table reports recall and throughput only.
- **The prefetch is not earning its place.** Isolated at `float32` it straddles zero
  (+4.9%/+2.1% RF3, -3.4%/-4.9% RF1). It should either be shown to pay on some workload or be
  turned off.
- **The `int32` overflow bound is implicit.** The 8.3x headroom follows from `VECTOR_MAX_DIM`; no
  assertion ties them together, so raising that limit past ~133,000 needs it rechecked.
