---
title: pgvector extension
headerTitle: pgvector extension
linkTitle: pgvector
description: Using the pgvector extension in YugabyteDB
tags:
  feature: tech-preview
menu:
  v2025.1:
    identifier: extension-pgvector
    parent: pg-extensions
    weight: 20
rightNav:
  hideH4: true
type: docs
---

The [pgvector](https://github.com/pgvector/pgvector) PostgreSQL extension allows you to store and query vectors, for use in performing similarity searches.

YugabyteDB includes pgvector `0.8.0-yb-1.0` on PostgreSQL 15-compatible YSQL. Most SQL-level types, functions, operators, casts, and aggregates match upstream pgvector 0.8.0. Approximate nearest neighbor (ANN) search uses the distributed `ybhnsw` access method (backed by DocDB Vector LSM) instead of native PostgreSQL `hnsw` / `ivfflat`.

Vector distance functions measure similarity or difference between high-dimensional data points. Choosing the right function depends on the use case, such as search, ranking, or clustering. YugabyteDB supports the following distance functions:

- Cosine Distance - Measures the angle between two vectors. Used for comparing direction rather than magnitude. Best for text similarity and recommendation systems.
- L2 (Euclidean) Distance - Measures the straight-line distance between two points in space. Best when absolute differences in values matter, like in image recognition.
- Inner Product - Measures similarity by multiplying corresponding elements and summing them. Often used in ranking and recommendation models, where larger values indicate higher similarity.

## Supported features

The following pgvector capabilities are supported in YugabyteDB.

### Types and SQL

| Feature | Details |
| :--- | :--- |
| `vector`, `halfvec`, and `sparsevec` types | Same user-facing SQL as upstream pgvector 0.8.0 (up to 16,000 dimensions for `vector` / `halfvec`) |
| Distance operators | `<->` (L2), `<#>` (inner product), `<=>` (cosine), `<+>` (L1) |
| Bit distance functions | `hamming_distance` and `jaccard_distance` |
| Vector functions | Including `l2_distance`, `inner_product`, `cosine_distance`, `l1_distance`, `l2_normalize`, `subvector`, `binary_quantize`, `vector_dims`, and `vector_norm` |
| Aggregates | `avg` and `sum` on `vector` and `halfvec` |
| Casts and array input | Casts among `vector`, `halfvec`, `sparsevec`, and arrays (`integer[]`, `real[]`, `double precision[]`, `numeric[]`) |
| DML and `COPY` | `INSERT`, `UPDATE`, `DELETE`, text `COPY`, and binary `COPY` for `vector`, `halfvec`, and `sparsevec` |

### Exact and approximate search

| Feature | Details |
| :--- | :--- |
| Exact (sequential) search | Supported for all distance metrics, including L1 (`<+>`) and queries on `halfvec` / `sparsevec` |
| HNSW ANN indexes on `vector` | `ybhnsw` with `vector_l2_ops`, `vector_ip_ops`, and `vector_cosine_ops` |
| `USING hnsw` compatibility | Rewritten internally to `ybhnsw` for PostgreSQL migration compatibility |
| Indexed dimensions | Up to 16,000 dimensions on `ybhnsw` (column must declare fixed dimensions, for example `vector(768)`) |
| Index options | `m`, `m0`, and `ef_construction` |
| Query-time tuning | `hnsw.ef_search` |
| Materialized views | Vector columns and `ybhnsw` indexes on materialized views |

Top-k queries of the form `ORDER BY embedding <-> query LIMIT k` use the `ybhnsw` index. You can combine ANN search with a `WHERE` filter; the filter is applied after the index scan.

## Enable the extension

To enable the pgvector extension:

```sql
CREATE EXTENSION vector;
```

## Create vectors

Create a vector column with 3 dimensions:

```sql
CREATE TABLE items (id bigserial PRIMARY KEY, embedding vector(3));
```

Insert vectors:

```sql
INSERT INTO items (embedding) VALUES ('[1,2,3]'), ('[4,5,6]');
```

Get the nearest neighbors by L2 distance:

```sql
SELECT * FROM items ORDER BY embedding <-> '[3,1,2]' LIMIT 5;
```

The extension also supports inner product (`<#>`) and cosine distance (`<=>`).

Note: `<#>` returns the negative inner product because PostgreSQL only supports `ASC` order index scans on operators.

## Store vectors

Create a new table with a vector column:

```sql
CREATE TABLE items (id bigserial PRIMARY KEY, embedding vector(3));
```

Or add a vector column to an existing table:

```sql
ALTER TABLE items ADD COLUMN embedding vector(3);
```

Insert vectors:

```sql
INSERT INTO items (embedding) VALUES ('[1,2,3]'), ('[4,5,6]');
```

Upsert vectors:

```sql
INSERT INTO items (id, embedding) VALUES (1, '[1,2,3]'), (2, '[4,5,6]')
    ON CONFLICT (id) DO UPDATE SET embedding = EXCLUDED.embedding;
```

Update vectors:

```sql
UPDATE items SET embedding = '[1,2,3]' WHERE id = 1;
```

Delete vectors:

```sql
DELETE FROM items WHERE id = 1;
```

## Query vectors

Get the nearest neighbors to a vector:

```sql
SELECT * FROM items ORDER BY embedding <-> '[3,1,2]' LIMIT 5;
```

Get the nearest neighbors to a row:

```sql
SELECT * FROM items WHERE id != 1 ORDER BY embedding <-> (SELECT embedding FROM items WHERE id = 1) LIMIT 5;
```

Get rows within a certain distance:

```sql
SELECT * FROM items WHERE embedding <-> '[3,1,2]' < 5;
```

<!--Note: Combine with `ORDER BY` and `LIMIT` to use an index.-->

### Distances

Get the distance:

```sql
SELECT embedding <-> '[3,1,2]' AS distance FROM items;
```

For inner product, multiply by -1 (`<#>` returns the negative inner product)

```sql
SELECT (embedding <#> '[3,1,2]') * -1 AS inner_product FROM items;
```

For cosine similarity, use 1 - cosine distance:

```sql
SELECT 1 - (embedding <=> '[3,1,2]') AS cosine_similarity FROM items;
```

### Aggregates

Average vectors:

```sql
SELECT AVG(embedding) FROM items;
```

Create a table with a vector column and a category column:

```sql
CREATE TABLE items (id bigserial PRIMARY KEY, embedding vector(3), category_id int);
```

Insert multiple vectors belonging to the same category:

```sql
INSERT INTO items (embedding, category_id) VALUES ('[1,2,3]', 1), ('[4,5,6]', 2), ('[3,4,5]', 1), ('[2,3,4]', 2);
```

Average groups of vectors belonging to the same category:

```sql
SELECT category_id, AVG(embedding) FROM items GROUP BY category_id;
```

## Vector indexing

{{<tags/feature/tp idea="1111">}} By default, vector search performs exact nearest neighbor search, ensuring perfect recall.

To improve query performance, you can use approximate nearest neighbor (ANN) search, which trades some recall for speed. Unlike traditional indexes, approximate indexes may return different results for queries.

YugabyteDB currently supports the [HNSW (Hierarchical Navigable Small World)](https://github.com/pgvector/pgvector?tab=readme-ov-file#hnsw) index type.

{{< note title="Vector indexes and upgrades" >}}
An upgrade from v2025.1 fails if a vector index exists. Dropping the indexes and recreating them after the upgrade is not enough. Follow the steps in [Upgrading](#upgrading).
{{< /note >}}

### HNSW

HNSW indexing creates a multilayer graph to enable efficient high-dimensional vector search. HNSW offers faster query performance but requires more memory and has longer build times. You can create an index before inserting any data into the table.

Add an index for each distance function you want to use.

To use the L2 distance function:

```sql
CREATE INDEX NONCONCURRENTLY ON items USING ybhnsw (embedding vector_l2_ops);
```

To use the inner product function:

```sql
CREATE INDEX NONCONCURRENTLY ON items USING ybhnsw (embedding vector_ip_ops);
```

To use the Cosine distance function:

```sql
CREATE INDEX NONCONCURRENTLY ON items USING ybhnsw (embedding vector_cosine_ops);
```

ANN indexes are supported on the `vector` type. You can store and run exact searches on `halfvec` and `sparsevec` columns; for HNSW indexing, use a `vector` column (or cast / densify to `vector`).

#### HNSW index options

You can fine-tune HNSW indexing using the following parameters:

| Parameter | Description | Default |
| :--- | :--- | :--- |
| m | Maximum number of connections per layer. Valid range: 5–64. | 32 |
| m0 | Maximum number of connections in the base layer. | Derived from `m` |
| ef_construction | Size of the dynamic candidate list for constructing the graph. Valid range: 50–1000. | 200 |

For example:

```sql
CREATE INDEX NONCONCURRENTLY ON items USING ybhnsw (embedding vector_l2_ops) WITH (m = 16, ef_construction = 128);
```

A higher `ef_construction` value provides faster recall at the cost of index build time / insert speed.

### Limitations

- Concurrent index creation is not currently supported. For example, the following syntax falls back to non-concurrent implementation:

    ```sql
    CREATE INDEX CONCURRENTLY on <table> USING ybhnsw (vec vector_l2_ops);
    ```

    Unlike concurrent index creation on non-vector data types, the index backfill will take an exclusive lock (ACCESS_EXCLUSIVE) on the table, and writes to the table are blocked while index backfill is in progress. {{<issue 26402>}}

- Partial indexes on vector columns are not supported yet. {{<issue 31441>}}
- Vector indexes are not supported for [xCluster replication](../../../architecture/docdb-replication/async-replication/).
- [Inspect at PIT](../../../manage/backup-restore/point-in-time-recovery/inspect/) (time travel queries) is not currently supported. {{<issue 20829>}}
- Restoring a backup onto a cluster with tablet splitting enabled can omit rows from a vector index. ANN queries on the restored index can then return incomplete results without an error. After the restore, drop and rebuild the vector indexes, or disable tablet splitting before the restore. {{<issue 20500>}}
- Backup restore across major versions is not supported for a database that contains vector indexes.

## Upgrading

An upgrade from v2025.1 fails if a vector index exists. Dropping the indexes and recreating them after the upgrade is not enough. Export the data, drop the indexes and the tables, and recreate the tables on v2025.2.6.0 or later, or on v2026.1.2.0 or later.

1. Before upgrading, save the DDL of each vector table and its indexes, and export the table data.
1. Drop the vector indexes, then drop the tables.
1. Upgrade to v2025.2.6.0 or later, or to v2026.1.2.0 or later, and finalize the upgrade.
1. Recreate each table from its DDL, create the vector indexes, and then load the exported data.

The reverse-mapping format is fixed when the table is created. Recreating the tables on v2025.2.0 through v2025.2.5, or on v2026.1.0, leaves them on the old format, and that storage is not reclaimed. If you upgrade to v2025.2.6.0, go next to v2026.1.2.0 or later. Upgrading from v2025.2.6.0 to v2026.1.0.x or v2026.1.1.x is not supported.

### Upgrading from v2024.2

A database with the `vector` extension installed cannot be upgraded in place from v2024.2. The `vector` type OID in v2024.2 is assigned when the extension is created and does not match the fixed OID in v2025.1. pgvector also changes from 0.4.4 to 0.8.0 with no update path, and the PostgreSQL 11 to 15 upgrade fails while the extension is installed. Export the data before dropping anything.

1. Save the DDL of every table that uses the `vector` type (for example, with `ysql_dump --schema-only`) and export the data (for example, with `\copy <table> TO '<table>.csv' WITH (FORMAT csv)`).
1. Drop those tables, then run `DROP EXTENSION vector;` in each database where the extension is installed.
1. Upgrade and finalize.
1. Run `CREATE EXTENSION vector;`, recreate the tables, and load the exported data.

v2024.2 has no vector indexes. v2025.1 stores a per-row identifier that vector indexes require, and rows written in v2024.2 do not have it. Loading the exported data into the recreated tables writes the rows in the new format.

### Vector index backend flag

`vector_index_backend` replaces `vector_index_use_yb_hnsw` and `vector_index_use_hnswlib` in v2025.2.3.0 and in v2026.1. Valid values are `yb_hnsw_hnswlib` (the default) and `yb_hnsw_usearch`. v2025.1 does not define this flag, and neither does v2025.2.0 through v2025.2.2.

If either old flag is set, remove it and set `vector_index_backend` as part of the upgrade, in the same rolling restart that moves to v2025.2.3.0 or later. The old flags are deprecated: they no longer select the backend, and the process logs a deprecation warning when they are set.

## Learn more

- Tutorial: [Build and Learn](/stable/develop/tutorials/build-and-learn/)
- Tutorials: [Build scalable generative AI applications with YugabyteDB](/stable/develop/ai/)
- [PostgreSQL pgvector: Getting Started and Scaling](https://www.yugabyte.com/blog/postgresql-pgvector-getting-started/)
- [Multimodal Search with PostgreSQL pgvector](https://www.yugabyte.com/blog/postgresql-pgvector-multimodal-search/)
