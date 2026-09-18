# Vector index coordinate quantization

DocDB vector indexes (`ybhnsw`) can store the coordinates of a served chunk in a narrower encoding
than `float32`: either `float16`, or `int8` paired with a `float16` rerank tier. The HNSW graph is
always built at full `float32` precision -- only the copies written into the immutable chunk file
are narrowed.

`float32` is the default. A `float32` index writes version 1 of the chunk format and dispatches
the same distance kernels it always has, so an index that does not opt in is unaffected.

## What to expect

VectorDBBench `Performance768D1M` (Cohere, 768 dimensions, 1M vectors, cosine), `k=100`,
30 concurrent clients, `LIMIT 100`. Baseline is `master` without this PR. Deltas are against the
baseline **at the same `ef_search`**, which is not the same as at the same recall -- see the note
under the `int8` rows.

### RF3, 3 nodes

| configuration | ef | recall | QPS | recall delta | QPS delta |
|---|---|---|---|---|---|
| baseline (`master`) | 100 | 0.9450 | 1973.9 | -- | -- |
| baseline (`master`) | 200 | 0.9781 | 1527.3 | -- | -- |
| PR, `float32` | 100 | 0.9470 | 2070.6 | +0.21% | +4.90% |
| PR, `float32` | 200 | 0.9785 | 1559.7 | +0.04% | +2.12% |
| PR, `float16` | 100 | 0.9463 | 2309.5 | +0.14% | +17.00% |
| PR, `float16` | 200 | 0.9787 | 1718.3 | +0.06% | +12.50% |
| PR, `int8`, overfetch 2 | 100 | 0.9783 | 1861.3 | +3.52% | -5.70% |
| PR, `int8`, overfetch 2 | 200 | 0.9783 | 1897.3 | +0.02% | +24.23% |
| PR, `int8`, overfetch 1 | 100 | 0.9454 | **2585.0** | +0.04% | **+30.96%** |
| PR, `int8`, overfetch 1 | 200 | 0.9782 | **2134.8** | +0.01% | **+39.78%** |

### RF1, 1 node

| configuration | ef | recall | QPS | recall delta | QPS delta |
|---|---|---|---|---|---|
| baseline (`master`) | 100 | 0.8787 | 1758.7 | -- | -- |
| baseline (`master`) | 200 | 0.9468 | 1309.8 | -- | -- |
| PR, `float32` | 100 | 0.8811 | 1699.5 | +0.27% | -3.37% |
| PR, `float32` | 200 | 0.9476 | 1245.8 | +0.08% | -4.88% |
| PR, `float16` | 100 | 0.8791 | 2057.3 | +0.05% | +16.98% |
| PR, `float16` | 200 | 0.9466 | 1579.2 | -0.02% | +20.57% |
| PR, `int8`, overfetch 2 | 100 | 0.9469 | 1727.1 | +7.76% | -1.80% |
| PR, `int8`, overfetch 2 | 200 | 0.9469 | 1739.0 | +0.01% | +32.77% |
| PR, `int8`, overfetch 1 | 100 | 0.8759 | 2120.1 | -0.32% | +20.55% |
| PR, `int8`, overfetch 1 | 200 | 0.9362 | 1730.4 | -1.12% | +32.12% |

### What each change is worth

**`float32` is the control, and it isolates the search hot path changes** (neighbour prefetch and
batched block cache metrics -- see below), since no coordinate encoding changes. It is neutral:
+4.9%/+2.1% on RF3, -3.4%/-4.9% on RF1. That spread straddles zero, so the prefetch work is not
currently earning its place and should not be quoted as a gain.

**`float16` is worth +12% to +21% at recall parity**, and halves the coordinate bytes. Recall moves
by at most 0.14% in either direction across all four cells, which is what makes it a safe default
to consider.

**`int8` at overfetch 1 is the throughput configuration: +31% and +40% on RF3 at recall parity.**
On RF1 it is +21%/+32% but gives up recall -- 1.1 points at `ef=200` -- so overfetch 1 is not free
everywhere, and RF1 wants overfetch 2.

**The `int8` overfetch-2 rows read as a regression at `ef=100` and are not one.** With `LIMIT 100`
the candidate budget is `max(ef, k, factor * k)` = 200 regardless of whether `ef` is 100 or 200,
so both rows run the same search: recall is byte-identical at 0.9783/0.9783 on RF3 and
0.9469/0.9469 on RF1, and `ef` is inert. Compared at equal budget -- the `ef=200` rows -- `int8`
is +24% to +33%. The `ef=100` row is comparing a 200-candidate search against the baseline's
100-candidate one, and the recall it bought -- 0.9450 to 0.9783 on RF3, 0.8787 to 0.9469 on RF1 --
is what it spent the throughput on.

### Why this is not 2x, and will not become 2x

Fitting `speedup = 1/((1-f) + f/c)` across all eight recall-matched cells, where `c` is the
coordinate-byte ratio versus `float32` (1.99x for `float16`, 3.92x for `int8`), gives
**f = 0.30 +/- 0.06** -- consistently, across two cluster shapes, two encodings and two `ef`
values. Fitting `f` from `int8` alone predicts the `float16` result to within 0.02-0.10x in every
cell.

Only about 30% of end-to-end query time scales with coordinate bytes. The ceiling for *any*
coordinate encoding, including a hypothetical free one, is therefore `1/(1-0.30)` = **1.43x**, and
`int8` at overfetch 1 already reaches ~93% of it. The remaining 70% is tablet fan-out (each tablet
runs its own search and the results merge), resolving and fetching 100 rows per query, the
neighbour-list block reads that no coordinate encoding shrinks, and YSQL and RPC overhead.

This is the reason a narrower encoding than `int8` is not worth building. `int4` would buy roughly
3% here. Published 2x results from engines where the distance kernel is nearly the whole query do
not transfer to a configuration where it is under a third of it.

## Record layout

`VectorStorageKind` names the traversal encoding and `RerankStorageKind` the optional second copy.
Per vector at 768 dimensions:

| encoding | traversal | rerank | record | vs `float32` | bytes per distance |
|---|---|---|---|---|---|
| `float32` | 3072 | -- | 3092 | 1.00x | 3072 |
| `float16` | 1536 | -- | 1556 | 0.50x | 1536 |
| `int8` | 768 | 1536 | 2324 | 0.75x | **768** |

The 20-byte overhead is the per-vector `YbHnswVectorData` header. The traversal touches thousands
of records per query while the rerank tier reads only the retained candidates, so `int8` spends
0.75x the disk to make the hot path read a quarter of the bytes.

`coordinate_codec.{h,cc}` owns both directions of the conversion, and nothing else narrows
coordinates: the writer and the query path must round identically, or a vector stops being at
distance zero from itself and the graph's neighbour choices stop agreeing with the distances used
to search it.

## The rerank tier, and why `ef` is not the knob

`int8` quantizes each coordinate to a per-chunk step, which costs several points of recall@k.
**Raising `ef` does not recover it** -- a search retains `max_num_results` entries ranked by that
same quantized distance, so widening the candidate set does not improve the final selection. For
scalar quantization `ef` is not a recall knob.

What recovers it is retaining more candidates than requested and rescoring them at `float16`
before they leave the chunk. `MakeResult` is the only point at which distances cross the chunk
boundary, so it is also the only point at which they must be in the metric's own units for
VectorLSM to merge them against other chunks.

The over-fetch is free while `factor x k <= ef`, since the search already maintains `max(ef, k)`
candidates. Above that the candidate budget rises to `factor x k` and the traversal does
proportionally more work -- the case to watch when querying with a large `LIMIT`.

## Per-chunk quantization scale

`int8` uses a symmetric scale, `max|coordinate| / 127`, derived from the chunk's own coordinates
and recorded in its footer. Scoping it to the chunk keeps it a pure function of data already in
hand -- no sampling, no configuration, nothing to recalibrate as an index grows -- at the cost that
two chunks of one index hold different scales. A query must therefore be quantized against the
scale of the chunk it is compared against, never a freshly computed one.

This is also why compaction reads the **rerank** copy rather than decoding the traversal
coordinates. A merged chunk derives a new scale, so decoding `int8` would feed already-quantized
values into a fresh quantization and lose a little more on every merge. The `float16` rerank copy
round-trips exactly, making a vector a fixed point across arbitrarily many compactions.

## Chunk format

Each chunk records its own encoding in its footer, under a serialization version:

| version | contents |
|---|---|
| 1 | base layout; always `float32` |
| 2 | adds `Header::storage_kind` |
| 3 | adds `Header::rerank_kind` and `Header::quantization_scale` |

The writer emits the lowest version that can represent the header, which is what keeps a `float32`
index at version 1. `Load` rejects a version it does not recognise rather than reading the fields
it knows and treating the remainder as block offsets, and `ValidateHeader` then checks the parsed
header against itself before any record is read through it -- a `vector_data_size` disagreeing with
the encodings would otherwise read past every record into the next, and an unusable quantization
scale would decode coordinates to zero or infinity.

## SIMD kernels

x86-64 builds at `-march=ivybridge`, so no AVX-512 family enables itself from its `-march` macro
and `usearch_include_wrapper_internal.h` forces the three SimSIMD targets the encodings need:
`HASWELL`, `SKYLAKE` and `ICE`. Forcing a target is safe -- each kernel carries its own target
attribute so it compiles under the Ivy Bridge baseline, and `simsimd_capabilities()` dispatches on
CPUID, so a kernel the host cannot run is never called.

`ICE` is the target the narrowed encodings depend on. SimSIMD has no `*_i8_skylake`, so without
it the `int8` ladder drops from AVX-512 straight to the 256-bit Haswell kernels while `float32`
keeps a 512-bit one. `cos/l2sq/dot_i8_ice` accumulate into `int32` exactly as the Haswell kernels
do, so they are bit-identical and cost no accuracy: `int8`'s measured recall is the quantization's
cost alone.

`float16` distances use the 256-bit `*_f16_haswell` kernels, which widen each coordinate to fp32
and accumulate there. That is the reason `float16` storage buys capacity rather than throughput --
halving the bytes does not halve the work when the kernel is narrower than `float32`'s.

## Search hot path

Three changes to the base layer search ship alongside the encodings. They are independent of
which encoding an index uses, and the `float32` rows in the results table isolate their combined
effect.

**Neighbour prefetch** (`yb_hnsw_prefetch_neighbors`, default on). Visiting a neighbour costs two
dependent cache misses that the CPU cannot speculate through: `blocks_[index]`, then the record
that slot points at. The loop therefore filters a node's neighbours through the visited set,
resolves all of their record addresses, and issues a prefetch for each, before computing any
distance -- so the misses overlap each other and the prefetches have the whole batch's distance
computations to land in. Both loop shapes call the same `visit` lambda in neighbour order, so
`best_dist` evolves identically and results are unchanged; `PrefetchMatchesSerialResolution`
asserts that, including equal block cache query and hit counts. The visited filter has to stay
first, or resolving addresses for already-visited neighbours would `Take()` blocks the search does
not need.

As measured, this is neutral (see Caveats). It is kept behind a runtime flag so it can be
switched off without a build, and so the two shapes can be A/B'd in one process.

**Batched block cache counters.** `cache_query` and `cache_hit` were incremented inside
`CachedBlock::Take`, which a single query calls thousands of times; the increments themselves
showed up on the hot path. `Take` now reports residency through a `was_hit` out-parameter and
`SearchCache` accumulates per search, flushing once in `Release()`. Totals are unchanged -- `Take`
is the only site feeding these counters -- only the update granularity is coarser. A caller
passing `nullptr` for `was_hit` gets no accounting at all, which is why the header says so.

**`take_wait_us` metric fix.** `BlockCacheMetrics` was instantiating `take_wait_us` from
`METRIC_vector_index_cache_read_us`, so both members pointed at the same prototype and the
take-wait histogram reported read latency. Anyone who read that metric before this fix was reading
`cache_read_us` under a second name.

## Enabling an encoding

A **master** flag chooses the encoding, which is stamped into the index's catalog entry at
`CREATE INDEX`. It is therefore fixed for the life of that index and identical on every replica,
rather than following each tserver's local flags.

```bash
# On every yb-master. Runtime-settable; no restart needed.
--vector_index_storage_coordinate_type=int8      # or float16, or float32 (default)
```

```bash
# On every yb-tserver. 1 disables the over-fetch, making reranking a no-op. Valid range 1-64.
--vector_index_rerank_overfetch_factor=2         # default
```

Which value to use depends on `LIMIT` relative to `ef_search`, because the candidate budget is
`max(ef_search, LIMIT, factor * LIMIT)`:

- **`factor * LIMIT <= ef_search`**: the over-fetch is free. The search already retains that many
  candidates, so raising the factor reranks more of them at no extra traversal cost, and
  `ef_search` still controls depth.
- **`factor * LIMIT > ef_search`**: the budget becomes `factor * LIMIT` and `ef_search` goes inert
  below it. At the default 2 with `LIMIT 100`, an `ef_search` of 100 and of 200 run the identical
  search. Set the factor to 1 and let `ef_search` control depth, or raise `ef_search` past
  `factor * LIMIT` so it means something again.

The RF3 numbers above are the first case at `LIMIT 100`: factor 1 is +31%/+40% at recall parity,
where factor 2 turns the same flag into an `ef_search` the operator did not ask for. RF1 is the
counter-example -- factor 1 costs it 1.1 recall points at `ef=200` -- so this is a per-deployment
choice, not a universally better default. It is a process-wide gflag today; a per-query search
option would let a client trade recall for latency per statement, which is what the knob wants to
be.

An index keeps the encoding it was created with, so the flag's value at query time says nothing
about an index created earlier. To see what an index actually got, read the footer from the index
flush log lines:

```
YbHnsw ... header: { dimensions: 768 vector_data_size: 2324 ... storage_kind: kInt8
                     rerank_kind: kFloat16 quantization_scale: 0.00418465 }
```

`vector_data_size` is the giveaway: `20 + 4*d` for `float32`, `20 + 2*d` for `float16`, and
`20 + 3*d` for `int8`.

### Downgrade

Selecting a narrow encoding is a one-way step for the chunks written while it is selected. A
version-2 or version-3 chunk is unreadable by a binary that only understands version 1. Clearing
the flag stops new chunks from using the encoding but does not rewrite existing ones, and an index
created while the flag was set keeps its encoding in the catalog. Moving such a cluster to a
binary without these encodings requires those indexes be dropped and rebuilt.

## Scope

The SimSIMD `#define`s take effect only in translation units that include
`usearch_include_wrapper_internal.h`, directly or through `hnsw.h`. That is 9 files: 5 production
(`hnsw.cc`, `yb_hnsw_wrapper.cc`, `hnswlib_wrapper.cc`, `usearch_wrapper.cc`, `hnsw_options.cc`)
and 4 tests. Every production one is vector-index code, and `hnsw.h` is included by no other
header, so there is no further fan-out and nothing in `tserver`, `master`, `rocksdb` or `postgres`
is reached. hnswlib's own SIMD is independent: it gates on `__AVX512F__`, which `-march=ivybridge`
does not define.

Disassembly confirms every AVX-512 instruction sits in a symbol suffixed `_ice` or `_skylake`. The
`ICE` kernels add about 5 KB of text, of which 3 of 7 are reachable from any YB code path. With
the default `float32` encoding the target is a runtime no-op, because the `float32` ladder has no
`ICE` rung.

Vector indexes are **YSQL-only**. YCQL cannot create one -- `add_vector_options` has no callers and
a CQL `DataType::VECTOR` hits `FATAL_INVALID_ENUM_VALUE` -- so neither the encodings nor the SIMD
targets can reach a YCQL workload.

`int32` accumulator headroom for `int8` inputs is 8.3x: saturation needs `d > 133,144` against a
`VECTOR_MAX_DIM` of 16,000.

## Tests

- `coordinate_codec-test.cc` -- round-trip error bounds for both encodings, clamping of
  unrepresentable values and NaN, and that one input always narrows to the same bytes.
- `hnsw-test.cc` -- that the prefetching and serial neighbour loops visit the same blocks and
  return identical results, so the flag cannot change an answer; that reranking recovers what
  `int8` costs *and* that the un-reranked
  configuration is measurably worse, without which the test would pass just as happily when the
  rerank tier does nothing; that over-fetch works when `ef <= max_num_results`, the regime a naive
  candidate budget silently breaks; that an infinite coordinate cannot set the quantization scale.
- `yb_hnsw_storage-test.cc` -- iteration and `Distance` through the `VectorIndexIf` stack, and that
  compaction reads the rerank copy rather than decoding `int8`.
- `pg_vector_index-test.cc` -- the same suite against each encoding end-to-end through flush,
  restart, compaction and the query path; that the distance the user sees is unaffected by the
  storage encoding; and that the tserver over-fetch flag is wired through.

## Caveats

- **The SIMD kernels have no sanitizer coverage.** SimSIMD is disabled under ASAN and TSAN, whose
  builds therefore exercise usearch's scalar metric. The wide intrinsic loads in the AVX-512
  kernels largely evade instrumentation, so a bad read inside one faults or is missed rather than
  being reported.
- **One workload.** Every number above is Cohere 768d/1M, `k=100`, 30 clients, read-only. The
  Skylake/ICE dispatch split, the 30% byte-proportional fraction, and the balance between bytes
  moved and kernel cost will differ on another host, at another dimension count, at a different
  `LIMIT`, or under a mixed read/write load.
- **The `int32` overflow bound is implicit.** The 8.3x headroom follows from `VECTOR_MAX_DIM`; no
  assertion ties the two together, so raising that limit past ~133,000 would need it rechecked.
- **Latency is unmeasured in the runs above.** The RF1/RF3 tables report recall and throughput
  only. An earlier single-node run had `float16` raising serial latency and p99 while `int8`
  lowered both; that has not been reconfirmed on these clusters, so treat per-query latency as
  unknown rather than assuming it tracks throughput.
- **The prefetch is not earning its place.** The `float32` rows isolate it and straddle zero
  (+4.9%/+2.1% on RF3, -3.4%/-4.9% on RF1). It is on by default and recall-neutral by
  construction, but it should either be shown to pay on some workload or be turned off.
- **Include order in `hnsw.cc`.** It includes `usearch/index.hpp` directly after `hnsw.h`, which
  already pulls in the wrapper. The wrapper's `#define`s land first, but reordering those includes
  would silently change which kernels compile in.
