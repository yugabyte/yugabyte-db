# Vector index coordinate quantization

DocDB vector indexes (`ybhnsw`) can store the coordinates of a served chunk in a narrower encoding
than `float32`: either `float16`, or `int8` paired with a `float16` rerank tier. The HNSW graph is
always built at full `float32` precision -- only the copies written into the immutable chunk file
are narrowed.

`float32` is the default. A `float32` index writes version 1 of the chunk format and dispatches
the same distance kernels it always has, so an index that does not opt in is unaffected.

## What to expect

VectorDBBench `Performance768D1M` (Cohere, 768 dimensions, 1M vectors, cosine), `k=100`,
`ef_search=200`, single-node `m8i.2xlarge`, RF1:

| encoding | QPS (30 clients) | vs `float32` | recall@100 | serial latency | p99 | load time | index size |
|---|---|---|---|---|---|---|---|
| `float32` (default) | 1337.3 | -- | 0.9471 | 3.58 ms | 37.98 ms | 327 s | 3.19 GB |
| `float16` | 1352.2 | +1.1% | 0.9471 | 4.60 ms | 41.45 ms | 331 s | 1.65 GB |
| `int8` + `float16` rerank | 1669.7 | **+24.9%** | 0.9464 | 3.21 ms | 32.46 ms | 327 s | 2.42 GB |

The two encodings solve different problems:

- **`int8`** is the throughput option: **+24.9% QPS for 0.07 recall points**, and the only encoding
  that also improves tail latency. Its record is *larger* than `float16`'s -- the rerank copy costs
  more than the narrowed traversal copy saves -- so it is not the way to shrink an index.
- **`float16`** halves the index (3.19 -> 1.65 GB) at identical recall, but is worth about 1% of
  throughput and *raises* serial latency and p99. Choose it for capacity, not for speed.

Load time is the same for all three: the 4-second spread is inside the run-to-run noise on this
benchmark, where two runs of one configuration differ by more than that.

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
do, so they are bit-identical and cost no accuracy: the measured 0.9464 recall for `int8` is the
quantization's cost alone.

`float16` distances use the 256-bit `*_f16_haswell` kernels, which widen each coordinate to fp32
and accumulate there. That is the reason `float16` storage buys capacity rather than throughput --
halving the bytes does not halve the work when the kernel is narrower than `float32`'s.

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
- `hnsw-test.cc` -- that reranking recovers what `int8` costs *and* that the un-reranked
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
- **One CPU, one workload.** Every number above is `m8i.2xlarge` on Cohere 768d/1M at RF1. The
  Skylake/ICE dispatch split and the balance between bytes moved and kernel cost will differ on
  another host, at another dimension count, or under a mixed read/write load.
- **The `int32` overflow bound is implicit.** The 8.3x headroom follows from `VECTOR_MAX_DIM`; no
  assertion ties the two together, so raising that limit past ~133,000 would need it rechecked.
- **`float16` costs latency.** It is the only encoding that makes serial latency and p99 worse.
  Treat it as a capacity option, not a performance one.
- **Include order in `hnsw.cc`.** It includes `usearch/index.hpp` directly after `hnsw.h`, which
  already pulls in the wrapper. The wrapper's `#define`s land first, but reordering those includes
  would silently change which kernels compile in.
