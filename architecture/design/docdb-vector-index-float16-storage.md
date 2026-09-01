# Using float16 vector index storage

Halves the coordinate bytes of every served vector index chunk. The HNSW graph is still built at
full `float32` precision; only the copy written into the immutable chunk file is narrowed, and
searches compute distances directly on those narrowed records.

## Enabling it

The encoding is chosen by a **master** flag and stamped into each index's catalog entry when the
index is created. It is therefore fixed for the life of an index, and identical on every replica.

```bash
# On every yb-master
--vector_index_storage_coordinate_type=float16     # or float32 (default)
```

Runtime-settable, so no restart is needed:

```bash
yb-ts-cli --server_address <master-host>:7100 \
  set_flag vector_index_storage_coordinate_type float16
```

Then create the index as usual — no schema or DDL change:

```sql
CREATE EXTENSION IF NOT EXISTS vector;

CREATE TABLE docs (
    id        bigserial PRIMARY KEY,
    embedding vector(1536)
);

-- Picks up whatever the master flag said at this moment, and keeps it forever.
CREATE INDEX docs_embedding_idx ON docs USING ybhnsw (embedding vector_l2_ops)
    WITH (m = 32, ef_construction = 200);
```

Queries are unchanged:

```sql
SET hnsw.ef_search = 40;

SELECT id, embedding <-> '[0.12, -0.44, ...]' AS distance
FROM docs
ORDER BY embedding <-> '[0.12, -0.44, ...]'
LIMIT 10;
```

### What the setting does and does not reach

| | |
|---|---|
| Applies to | Chunks written after the setting took effect, for indexes created while it was set |
| Does not apply to | The indexed table's `vector` column — still `float32` on disk |
| Does not apply to | The mutable (in-RAM) chunk and the insert registry — still `float32` |
| Does not apply to | Existing indexes — the encoding is fixed at `CREATE INDEX` |
| Does not apply to | The `usearch` backends (`YB_HNSW_USEARCH`, `USEARCH`); only the default `yb_hnsw_hnswlib` narrows |

Changing the flag afterwards affects only indexes created from then on. To move an existing index,
recreate it.

## Checking what an index is using

The encoding lives in the index's `HnswIndexOptionsPB.storage_type`, and each chunk file records
its own in the footer. Log lines from an index flush include the header:

```
YbHnsw ... header: { dimensions: 1536 vector_data_size: 3092 ... storage_kind: kFloat16 }
```

`vector_data_size` is the giveaway: `20 + 4·d` for float32, `20 + 2·d` for float16.

## What to expect

Coordinates dominate a served record at any realistic dimension count; the rest of the record —
the 20-byte header, the 16-byte vector id, and the base-layer neighbour list — is a fixed ≈260 B.

| Dimensions | float32 B/vector | float16 B/vector | Reduction | Records per 64 KiB block |
|---:|---:|---:|---:|---|
| 128 | 772 | 516 | 1.50× | 123 → 237 |
| 384 | 1 796 | 1 028 | 1.75× | 42 → 83 |
| 768 | 3 332 | 1 796 | 1.86× | 21 → 42 |
| 1536 | 6 404 | 3 332 | 1.92× | 10 → 21 |

**Accuracy.** Measured over the vendored fp16 implementation:

- Coordinates in fp16's normal range (magnitude ≥ 6.104e-5): round-trip relative error
  ≤ 4.87e-4, i.e. within 2⁻¹¹.
- Smaller magnitudes lose relative precision, but absolute error stays ≤ 2.98e-8, contributing
  under 1e-15 to a squared distance.

Both the stored record and the query are narrowed through the same function, so a vector is still
at distance exactly zero from itself.

**Distances users see are unchanged.** `ybvectorgettuple` never populates `xs_orderbyvals`, so
PostgreSQL evaluates `embedding <-> $query` itself, over the full-precision column value from the
heap tuple. Narrowing changes *which* rows come back and in what order — never the numbers in the
result.

**Throughput** is the part that does not have a single answer, because two different effects are
in play and which one dominates depends on the cluster. The two worked examples below are the
endpoints.

### Worked example A: large, does not fit in cache

100 M vectors at d = 1536, RF 3, 10 nodes × 128 GiB. Legacy memory defaults, so the block cache
is 50% of a 0.85 × 128 GiB hard limit = 54.4 GiB ≈ 58.4 GB. Each node holds 30 M vectors.

| | float32 | float16 |
|---|---:|---:|
| Served index per node | 30 M × 6 404 B = **192.1 GB** | 30 M × 3 332 B = **100.0 GB** |
| Resident fraction | 30.4% | 58.4% |
| Miss rate per visited vector | 69.6% | 41.6% |
| **Disk reads per query** | baseline | **÷ 1.67** |

Block size is fixed and the base-layer walk touches records in essentially random order, so
narrowing does not reduce the *number* of block touches. It shrinks the file, so more of it stays
resident and fewer touches miss. This is where fp16 earns the most, and where a small dimension
count or a large cache makes it earn nothing.

### Worked example B: small, already fits — VectorDBBench PERFORMANCE768D1M

Cohere, 768 dimensions, 1 M vectors. RF 3 on 3 × m8i.2xlarge (8 vCPU, 32 GiB). With RF 3 on
exactly 3 nodes **every node holds a full replica**, so each node serves all 1 M vectors.

The index is small either way:

| Per node | float32 | float16 |
|---|---:|---:|
| Served vector index | 1 M × 3 332 B = **3.33 GB** | 1 M × 1 796 B = **1.80 GB** |

But the block cache is shared with DocDB, and **the indexed table is about as large as the
index** — it stores the same 768 float32s per row:

| Per node, in the same block cache | Bytes |
|---|---:|
| Table SSTs (`vector(768)` value ≈ 3 091 B/row + key, packed-row and SST index/filter overhead) | ≈ 3.1 GB |
| Reverse mapping (`vector_id → ybctid`) | ≈ 50 MB |
| **Vector index, float32** | 3.33 GB |
| **Combined working set, float32** | **≈ 6.4 GB** |
| **Combined working set, float16** | **≈ 4.9 GB** |

And how much cache there is depends on one flag:

| Configuration | TServer hard limit | Block cache | float32 working set fits? |
|---|---:|---:|---|
| Legacy defaults | 0.85 × 32 GiB = 27.2 GiB | 50% = 13.6 GiB ≈ **14.6 GB** | Comfortably — 6.4 of 14.6 GB |
| `--use_memory_defaults_optimized_for_ysql=true` | 0.60 × 32 GiB = 19.2 GiB | 32% = 6.14 GiB ≈ **6.6 GB** | **At the edge — 6.4 of 6.6 GB** |

So "the index fits in memory with fp32" is true and yet not the whole picture. **The answer differs
between those two rows:**

- **Legacy defaults (14.6 GB cache).** Everything is resident either way. The residency effect —
  the entire basis of example A — contributes *nothing*. What remains is the DRAM-traffic effect
  below, plus 1.5 GB of headroom and smaller files to move on remote bootstrap and backup.
- **YSQL-optimized defaults (6.6 GB cache).** The float32 combined working set is within 3% of the
  cache, before counting SST index/filter blocks and the mutable chunk's own reservation. That
  configuration will evict under load; float16's 4.9 GB will not. Here fp16 is clearly worth it,
  and the reason has nothing to do with the index being too big — it is the table crowding it out.

**The DRAM-traffic effect, which applies in both rows.** A 1.8–3.3 GB index does not fit in L3
(roughly 16–32 MB for an 8-vCPU slice), so each distance computation pulls its record from DRAM
even when the block cache holds it. At d = 768 that is 3 072 B (48 cache lines) for float32
against 1 536 B (24 lines) for float16. Halving the lines fetched is a real gain — but see the
next section, because on this hardware YugabyteDB's build partly cancels it.

Query narrowing costs about 0.9 µs per chunk per search at d = 768. With 1 M vectors at
`--vector_index_compaction_chunk_max_mem_store_size_percentage=60` the whole index fits in a
single chunk under either memory configuration, so that is ~1–2% of a query. Not a factor.

### The SIMD kernel asymmetry on AVX-512 hosts

YugabyteDB compiles x86-64 with `-march=ivybridge` (`CMakeLists.txt`), and
`usearch_include_wrapper_internal.h` force-enables exactly two SimSIMD target families:

```c
#define SIMSIMD_TARGET_HASWELL 1
#define SIMSIMD_TARGET_SKYLAKE 1
```

`SIMSIMD_TARGET_SAPPHIRE`, `_ICE` and `_GENOA` are left to SimSIMD's auto-detection, which keys
off `__AVX512FP16__`, `__AVX512VNNI__` and `__AVX512BF16__`. None of those are defined under
`-march=ivybridge` (verified: it defines only `__F16C__`), so **those kernel families are not
compiled at all.** The consequence at runtime on any AVX-512 machine, m8i included:

| Distance | Kernel selected | Vector width | Iterations at d = 768 | Bytes read |
|---|---|---:|---:|---:|
| float32 | `simsimd_l2sq_f32_skylake` | 512-bit, 16 floats | **48** | 3 072 |
| float16 | `simsimd_l2sq_f16_haswell` | 256-bit, 8 halves + 2×`vcvtph2ps` | **96** | 1 536 |
| float16 *if* `SIMSIMD_TARGET_SAPPHIRE` were on | `simsimd_l2sq_f16_sapphire` | 512-bit, 32 halves, native fp16 | **24** | 1 536 |

Both kernels accumulate into a single register, so the loop is bound by FMA latency: float16
currently pays roughly **twice the dependent-chain cycles** of float32 at the same dimension,
which eats into the halved memory traffic. Net effect is genuinely uncertain without measurement —
somewhere around 1.3–1.8× in favour of float16 when records come from DRAM, and *against*
float16 for any part of the working set hot enough to sit in L2.

Both `simsimd_*_sapphire` and `simsimd_*_ice` are guarded by
`#pragma clang attribute push(__attribute__((target(...))))`, the same mechanism that already lets
the Haswell and Skylake kernels compile under an Ivy Bridge baseline. So enabling them is a
one-line change and needs no `-march` bump — but it does need a compile check, and
`SIMSIMD_TARGET_ICE` is also what would make the AVX-512 VNNI int8 kernel
(`simsimd_l2sq_i8_ice`) available for any future int8 work. On m8i specifically, doing this before
evaluating fp16 is worth more than the fp16 change itself.

## Where it does not help

- **Mutable chunk memory.** Build-side per-vector footprint stays ≈`383 + 4·d` bytes (≈6.5 KB at
  d = 1536), and a full mutable chunk still reserves ≈650 MB per tablet at that dimension.
- **Chunk count.** The compaction output cap is sized from the *build* representation
  (`EstimateNumVectorsForBytes` with `sizeof(float)`), so halving served bytes does not produce
  fewer chunks. Per-query CPU is linear in chunk count, so this is a real limit on the benefit.
- **Read amplification from a fixed block size.** At d = 1536 a ~61 KB vector-data block yields
  one useful 6.4 KB record per random read. Narrowing makes each read *less* efficient while
  making misses rarer. `--yb_hnsw_max_block_size` (runtime, per tserver, default 64 KiB) is an
  independent and probably larger lever here — worth measuring alongside. Its cost is more
  blocks, each carrying a resident ~150 B `CachedBlock` plus a cache entry.
- **Dimensions below ~256.** The fixed ≈260 B of graph overhead per record starts to dominate.
- **Ingest.** `HnswlibIndex::Reserve` reserves the *build* footprint in the block cache up front.
  Backfilling 1 M vectors at d = 768 reserves 1 M × 3 455 B ≈ **3.5 GB**, which on a 6.6 GB cache
  is over half of it — during ingest, regardless of the storage encoding.

## Out-of-range coordinates

fp16 represents magnitudes up to 65504. Anything larger would saturate to infinity, and one
infinite coordinate makes every distance to that vector NaN, which silently corrupts the ordering
of the search heaps. So out-of-range values are clamped to ±65504 and NaN is replaced with zero,
and the flush logs a count:

```
YbHnsw /path/to/chunk: clamped 12 coordinate(s) that kFloat16 cannot represent;
searches involving those vectors will be less accurate
```

If that appears, the data is outside fp16's range and those vectors will rank badly. Every
mainstream embedding model produces coordinates well inside ±1, so this should not fire; if it
does, stay on `float32` (or see the bf16 note in the implementation plan — bf16 has float32's
exponent range).

## Upgrade and downgrade

A chunk file records its own encoding in a versioned footer, and the writer picks the lowest
version that can represent the header:

| | |
|---|---|
| float32 index, new binary | Writes **byte-identical version-1 files**. Readable by releases that predate this feature. |
| float16 index, new binary | Writes version-2 files. |
| Version-1 file, new binary | Read normally, treated as float32. |
| **Version-2 file, older binary** | **Not readable.** The old code reads the version byte and ignores it, so it misparses the footer. |

So enabling float16 is a one-way step for the chunks written while it is on:

1. All replicas must be on a release that understands the encoding before enabling it.
2. Turning it back off stops *new* chunks from being float16 but does not rewrite existing ones.
   A full compaction after flipping back is what restores downgradeability.

Mixed encodings within one index are supported by construction: each chunk builds its own metric
from its own footer, and compaction widens every source back to float32 before rebuilding.

## Deciding whether it is worth it

The deciding number is **not the index size** — it is how much block cache is left after the
indexed table has taken its share.

1. Get the actual block cache size from the TServer's `/varz`
   (`db_block_cache_size_percentage`, `default_memory_limit_to_ram_ratio`) rather than assuming
   the default; the two default sets differ by more than 2× on the same node.
2. Compare it against `table SST size + index size` on one node, not against the index alone.
   The `vector` column makes the table roughly as large as the index.
3. If the sum comfortably fits, the residency argument does not apply to you and the remaining
   gain is DRAM traffic per distance — real but modest, and partly cancelled by the kernel
   asymmetry above. Measure before adopting, and see
   [int8 vector index storage](docdb-vector-index-int8-storage.md), which is the encoding aimed at
   this case: a quarter of the bytes per distance and a wider kernel, at a larger record.
4. If the sum is near or above the cache, fp16 is doing real work and is worth adopting once
   recall checks out.
5. On an AVX-512 host, evaluate `SIMSIMD_TARGET_SAPPHIRE` first — otherwise the fp16 measurement
   is of a handicapped kernel and will understate the option.
6. Measure `--yb_hnsw_max_block_size` in the same pass; on a resident working set it is an
   independent lever on random-read amplification.
7. Always measure recall against a brute-force top-k on your own embeddings, and understand the
   one-way-door note above, before enabling in production.
