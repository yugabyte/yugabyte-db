# Using int8 vector index storage

Stores each vector twice in a served chunk record: as `int8` for the graph traversal, and as
`float16` for a final rerank of the candidates a search retained. The traversal touches a quarter
of the bytes `float32` does, and the whole record is 0.75× `float32` — so unlike
[float16 storage](docdb-vector-index-float16-storage.md), this trades *search cost* rather than
*disk*, and the two suit opposite situations. §"Which encoding" below is the decision.

The HNSW graph is still built at full `float32` precision. Only the copies written into the
immutable chunk file are narrowed.

## Enabling it

The encoding is chosen by a **master** flag and stamped into each index's catalog entry when the
index is created, so it is fixed for the life of an index and identical on every replica.

```bash
# On every yb-master
--vector_index_storage_coordinate_type=int8      # or float16, or float32 (default)
```

Runtime-settable, so no restart is needed:

```sql
-- Existing indexes keep whatever encoding they were created with.
CREATE INDEX ON documents USING ybhnsw (embedding vector_l2_ops) WITH (m=16, ef_construction=128);
```

Confirm what an index actually got — the flag's value at query time says nothing about an index
created earlier. The encoding lives in the index's `HnswIndexOptionsPB.storage_type`, and each
chunk file records its own in the footer, which index flush log lines include:

```
YbHnsw ... header: { dimensions: 768 vector_data_size: 2324 ... storage_kind: kInt8
                     rerank_kind: kFloat16 quantization_scale: 0.00418465 }
```

`vector_data_size` is the giveaway: `20 + 4·d` for float32, `20 + 2·d` for float16, `20 + 3·d`
for int8.

## Why there is a rerank tier

`int8` quantizes each coordinate to a per-chunk step, and on 768-dimensional normalized embeddings
that costs roughly 4–8 points of recall@10.

**Raising `ef` does not recover it.** A search keeps only `max_num_results` entries, ranked by
the same quantized distance, so widening the candidate set does not improve the final selection.
This is the one thing to understand before tuning: for scalar quantization `ef` is not a recall
knob.

What does recover it is retaining more candidates than requested and rescoring them at `float16`
before returning. Measured on synthetic 768-d data, retaining 2× the requested count puts the true
top-k inside the retained set in every regime tested, and 3× adds nothing. Hence:

```bash
# On every yb-tserver. 1 disables the over-fetch, which makes reranking a no-op.
--vector_index_rerank_overfetch_factor=2        # default
```

The over-fetch is free while `factor × k <= ef`: the search already maintains `max(ef, k)`
candidates, and retaining more of them with their ids just moves slots around. When
`factor × k > ef` — for example `k=100` with the default `ef=64` — the candidate budget rises to
`factor × k` and the traversal does proportionally more work. That is the case to watch if you
query with a large `LIMIT`.

## Bytes, per vector, at 768 dimensions

| encoding | traversal | rerank | record | vs float32 | bytes per distance |
|---|---|---|---|---|---|
| `float32` | 3072 | — | 3092 | 1.00× | 3072 |
| `float16` | 1536 | — | 1556 | 0.50× | 1536 |
| `int8` | 768 | 1536 | 2324 | **0.75×** | **768** |

The rerank copy is read only for the retained candidates — at `k=10` and 2× over-fetch that is
20 records per query against the thousands the traversal touches, so a couple of percent. At
`k=100` it is closer to 20% of the traversal, which is the other reason the factor is a flag.

## Which encoding

Both narrow the served copy; they are not ordered, and the choice turns on one measurement.

1. Get the **actual** block cache size from the TServer's `/varz`
   (`db_block_cache_size_percentage` × `default_memory_limit_to_ram_ratio` × RAM) rather than
   assuming a default — the two default sets differ by more than 2× on the same node.
2. Compare it against **table SST size + index size** on one node, not against the index alone.
   The index chunks, the table's SSTs and the reverse mapping share one `rocksdb::Cache`, and the
   `vector` column makes the table roughly as large as the index.
3. **If the sum does not fit**, residency is the binding constraint. Use `float16`: it is 0.50× the
   record where `int8` is 0.75×, so it is the one that gets more of the index into cache.
4. **If the sum fits comfortably**, residency is already satisfied and there is nothing left for
   `float16` to win. `int8` is the one that helps: a quarter of the bytes per distance, and on our
   compiled SIMD targets a wider kernel rather than a narrower one.
5. Either way, measure recall against a brute-force top-k **on your own embeddings** before
   adopting. Anisotropic embeddings — variance concentrated in leading dimensions — raise the
   per-chunk maximum magnitude and with it the quantization step, so they are the harder case for
   `int8` and the one worth measuring.

## The quantization scale is per chunk

Each chunk derives its own step from the largest magnitude it contains, recorded in its footer.
Two chunks of the same index therefore hold different scales, and this has consequences worth
knowing:

- Raw traversal distances are in quantized units and are only comparable *within* one chunk.
  Reranking is what puts them back into the metric's units before they leave, which is how results
  from several chunks — and from the not-yet-flushed mutable chunk — merge correctly.
- **Reported distances are unaffected by any of this.** The index scan does not hand its distance
  to the executor; PostgreSQL evaluates `<->` itself over the full-precision column value from the
  heap tuple. So `ORDER BY embedding <-> $1` returns exact distances at every encoding, and only
  *which* rows come back can differ.
- A single outlier vector sets the scale for its whole chunk. If a chunk's
  `quantization_scale` looks far larger than your embeddings' typical magnitude, that is what
  happened, and recall in that chunk will be correspondingly worse.
- A coordinate too large for the scale is clamped rather than wrapped, and the build logs a
  warning naming how many. Clamping is a real accuracy loss for those vectors; the warning is
  worth reacting to.

## Compaction

Compaction reads source chunks and re-inserts what it finds into a merged chunk, which derives a
new scale. It reads the **rerank copy**, not the quantized one — decoding `int8` and re-quantizing
against a different scale would compound the error once per merge. The `float16` copy round trips
exactly, so a vector's stored value is stable across arbitrarily many compactions.

Nothing to configure; it matters because it is why the rerank copy is not merely a search
optimization.

## Upgrade and downgrade

A chunk records its own encoding in a versioned footer, and the writer picks the lowest version
that can represent the header:

| | |
|---|---|
| `float32` index, new binary | Writes **byte-identical version-1 files**, readable by releases predating any of this. |
| `float16` index, new binary | Writes version-2 files. |
| `int8` index, new binary | Writes version-3 files. |
| Older version, new binary | Read normally at whatever encoding it records. |
| **Version-2 or -3 file, older binary** | **Not readable.** The old code reads the version byte and ignores it, so it misparses the footer. |

So enabling `int8` is a one-way step for the chunks written while it is on:

1. Every replica must be on a release that understands the encoding before enabling it. A tserver
   that predates `STORAGE_INT8` does not recognise the value in the index's catalog entry and
   falls back to `float32`, so replicas of the same tablet would build chunks in different
   encodings. Nothing is corrupted — each replica reads only its own files, and each file records
   its own encoding — but the replicas would answer with different recall, which is confusing to
   diagnose. The same applies to `float16`.
2. Turning it back off stops *new* chunks from being `int8` but does not rewrite existing ones. A
   full compaction after flipping back is what restores downgradeability.

Mixed encodings within one index are supported by construction: each chunk builds its metrics and
reads its scale from its own footer, and compaction widens every source before rebuilding.

## Limitations

- Only the `yb_hnsw_hnswlib` backend (the default) writes narrowed chunks. Under
  `--vector_index_backend=yb_hnsw_usearch` the setting is ignored and chunks are written at
  `float32`.
- The recall figures above are from synthetic data. Inner product is the metric most likely to
  disappoint, because it is normally used on un-normalized vectors where one large-magnitude
  vector sets the scale for everyone; measure it specifically if you use it.
