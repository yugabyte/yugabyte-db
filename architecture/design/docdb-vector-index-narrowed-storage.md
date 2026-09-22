# Vector index narrowed coordinate storage

A DocDB vector index (`ybhnsw`) can store the coordinates of a served chunk as `float16` rather
than `float32`. The HNSW graph is always built at full `float32` precision -- only the copy
written into the immutable chunk file is narrowed.

`float32` is the default and writes version-1 chunks, so an index that does not opt in is
unaffected.

## Record layout

`VectorStorageKind` names the encoding. Per vector at 768 dimensions:

| encoding | coordinates | record | vs `float32` | bytes per distance |
|---|---|---|---|---|
| `float32` | 3072 | 3092 | 1.00x | 3072 |
| `float16` | 1536 | 1556 | 0.50x | 1536 |

The 20-byte overhead is the per-vector `YbHnswVectorData` header.

`coordinate_codec.{h,cc}` owns both directions and nothing else narrows coordinates: writer and
query path must round identically, or a vector stops being at distance zero from itself and the
graph's neighbour choices stop agreeing with the distances used to search it.

## Measured

VectorDBBench `Performance768D1M` (Cohere, cosine), `k=100`, 30 clients, versus `float32` at the
same `ef_search`:

| setup | ef | recall delta | throughput |
|---|---|---|---|
| RF3 | 100 | +0.14% | +17.0% |
| RF3 | 200 | +0.06% | +12.5% |
| RF1 | 100 | +0.05% | +17.0% |
| RF1 | 200 | -0.02% | +20.6% |

Recall moves by at most 0.14% in either direction across all four cells, which is what makes the
encoding safe to consider as a default.

Throughput gains less than the halved byte count suggests, for two reasons. The `*_f16_haswell`
kernels widen each coordinate to fp32 and accumulate there, so halving the bytes does not halve
the work. And fitting `speedup = 1/((1-f) + f/c)` across these cells, `c` being the coordinate-byte
ratio, gives **f ~ 0.3**: only about 30% of end-to-end query time scales with coordinate bytes.
The rest is tablet fan-out, resolving and fetching `LIMIT` rows per query, neighbour-list reads
that no encoding shrinks, and YSQL/RPC overhead. That puts a ceiling near 1.43x on what any
coordinate encoding can buy here.

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

The writer emits the lowest version that can represent the header, which keeps a `float32` index
at version 1. `Load` rejects a version outside the supported range rather than reading the fields
it knows and consuming the remainder as block offsets, and `ValidateHeader` then checks the parsed
header against itself before any record is read through it: a `vector_data_size` disagreeing with
the encoding would read past every record into the next, and an out-of-range encoding enum would
reach `FATAL_INVALID_ENUM_VALUE` and abort the process rather than failing the one file.

## Search hot path

Two changes here are independent of which encoding an index uses.

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

## Enabling the encoding

A **master** flag chooses the encoding, stamped into the index's catalog entry at `CREATE INDEX`.
It is therefore fixed for the life of that index and identical on every replica, rather than
following each tserver's local flags.

```bash
# On every yb-master. Runtime-settable; no restart needed.
--vector_index_storage_coordinate_type=float16   # or float32 (default)
```

An index keeps the encoding it was created with, so the flag's value at query time says nothing
about an index created earlier. To see what an index got, read `vector_data_size` from the index
flush log line: `20 + 4*d` for `float32`, `20 + 2*d` for `float16`.

### Downgrade

Selecting `float16` is one-way for the chunks written while it is selected: a version-2 chunk is
unreadable by a binary that only understands version 1. Clearing the flag stops new chunks from
using the encoding but does not rewrite existing ones, and an index created while it was set
keeps its encoding in the catalog. Moving such a cluster to a binary without the encoding
requires those indexes be dropped and rebuilt.

## SIMD kernels

x86-64 builds at `-march=ivybridge`, so no AVX-512 family enables itself from its `-march` macro
and `usearch_include_wrapper_internal.h` forces the SimSIMD targets the encoding needs:
`HASWELL` and `SKYLAKE`. Forcing a target is safe -- each kernel carries its own target attribute
so it compiles under the Ivy Bridge baseline, and `simsimd_capabilities()` dispatches on CPUID, so
a kernel the host cannot run is never called.

`float16` distances use the 256-bit `*_f16_haswell` kernels, which widen each coordinate to fp32
and accumulate there. The `#define`s reach only translation units including
`usearch_include_wrapper_internal.h`, directly or through `hnsw.h`: 5 production files, all
vector-index code, plus 4 tests. `hnsw.h` is included by no other header, so nothing in
`tserver`, `master`, `rocksdb` or `postgres` is reached. Vector indexes are YSQL-only, so none of
this can reach a YCQL workload.

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
