---
title: pgvector extension
headerTitle: pgvector extension
linkTitle: pgvector
description: Using the pgvector extension in YugabyteDB
menu:
  stable:
    identifier: extension-pgvector
    parent: pg-extensions
    weight: 20
rightNav:
  hideH4: true
type: docs
aliases:
  - /stable/explore/ysql-language-features/pg-extensions/extension-pgvector
---

{{< note title="GA in v2026.1.1.0 or later" >}}

Vector indexes are GA starting in v2026.1.1.0 and v2025.2.6.0. These releases store vector tables in a new layout. Tables created on earlier releases retain the old layout. A table can't be converted in place, so each vector table created before these releases must be rebuilt once, from its DDL and DML, as part of the upgrade. See [Upgrade vector indexes](#upgrade-vector-indexes).

{{< /note >}}

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

By default, vector search performs exact nearest neighbor search, ensuring perfect recall.

To improve query performance, you can use approximate nearest neighbor (ANN) search, which trades some recall for speed. Unlike traditional indexes, approximate indexes may return different results for queries.

YugabyteDB currently supports the [HNSW (Hierarchical Navigable Small World)](https://github.com/pgvector/pgvector?tab=readme-ov-file#hnsw) index type.

### HNSW

HNSW indexing creates a multilayer graph to enable efficient high-dimensional vector search. HNSW offers faster query performance but requires more memory and has longer build times. You can create an index before inserting any data into the table.

Add an index for each distance function you want to use.

To use the L2 distance function:

```sql
CREATE INDEX NONCONCURRENTLY ON items USING ybhnsw (embedding vector_l2_ops);
```

For PostgreSQL backwards compatibility, `USING hnsw` is also supported and is internally mapped to the `ybhnsw` index access method. For example, the following statement is equivalent to the one above:

```sql
CREATE INDEX NONCONCURRENTLY ON items USING hnsw (embedding vector_l2_ops);
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

#### Query-time tuning

You can tune query-time behavior of HNSW search using the following GUC:

| GUC | Description | Default |
| :--- | :--- | :--- |
| hnsw.ef_search | Size of the dynamic candidate list for search. Valid range: 1–1000. Higher values improve recall at the cost of query latency. | 40 |

For example, to increase recall for the current session:

```sql
SET hnsw.ef_search = 100;
```

### Limitations

- Concurrent index creation is not currently supported. For example, the following syntax falls back to non-concurrent implementation:

    ```sql
    CREATE INDEX CONCURRENTLY on <table> USING ybhnsw (vec vector_l2_ops);
    ```

    Unlike concurrent index creation on non-vector data types, the index backfill will take an exclusive lock (ACCESS_EXCLUSIVE) on the table, and writes to the table are blocked while index backfill is in progress. {{<issue 26402>}}

- Partial indexes on vector columns are not supported yet. {{<issue 31441>}}
- Default values on vector columns are not supported when adding a column with `ALTER TABLE ... ADD COLUMN ... DEFAULT`. {{<issue 32936>}}
- Vector indexes are not supported for [xCluster replication](../../../architecture/docdb-replication/async-replication/), including xCluster DR.
- Vector indexes are not supported for [point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/) (PITR).
- Vector indexes are not supported for [instant database cloning](../../../manage/backup-restore/point-in-time-recovery/clone/).
- [Inspect at PIT](../../../manage/backup-restore/point-in-time-recovery/inspect/) (time travel queries) is not currently supported.

### Notes

- When [encryption at rest](../../../secure/encryption-at-rest/) is enabled, vector index files on tablet servers are not encrypted. These files hold the indexed vectors and the HNSW graph, but not the rows they belong to. Table data, including the vector column itself, is encrypted as usual.

## Upgrade vector indexes

Vector indexes are generally available starting in v2026.1.1.0, and v2025.2.6.0 in the v2025.2 LTS series. These releases store vector tables in a new layout. Tables created on earlier releases retain the old layout, and a table can't be converted in place, so each vector table created before these releases must be rebuilt once, from its DDL and DML, as part of the upgrade. Subsequent upgrades don't require extra steps for vector tables.

### Step 1: Choose the release to upgrade to

Vector indexes reached general availability recently and are improving quickly; each new release can include further vector index fixes. Upgrade to the latest release in your series:

- v2026.1 series: the latest v2026.1 release, and no earlier than v2026.1.2.0. v2026.1.1.x supports vector indexes, but v2026.1.2.0 includes important fixes for vector search errors during concurrent deletes, a vector index stall during compaction, and possible row loss after a crash. Tables created on v2026.1.1.x don't need to be rebuilt when you upgrade to a later v2026.1 release.
- v2025.2 LTS series: the latest v2025.2 release, and no earlier than v2025.2.6.0.

When upgrading from v2025.2.6.0 or later to v2026.1, upgrade to the latest v2026.1 release, and no earlier than v2026.1.2.0. v2026.1.0.x and v2026.1.1.x don't contain all the fixes in v2025.2.6.0, so they aren't supported upgrade targets.

### Step 2: Find the tables that need a rebuild

The upgrade procedure depends on the release each vector table was created on, not the release your universe runs today. YugabyteDB doesn't show which layout a table uses, so keep track of when your vector tables were created.

Run the following query in each database. It lists every table that has a vector column, with the DDL of its vector indexes, if any. Save the output; you'll need the index DDL to recreate the indexes.

```sql
SELECT n.nspname AS schema_name,
       c.relname AS table_name,
       v.index_name,
       v.index_ddl
FROM pg_class c
JOIN pg_namespace n ON n.oid = c.relnamespace
LEFT JOIN (
    SELECT x.indrelid, i.relname AS index_name, pg_get_indexdef(i.oid) AS index_ddl
    FROM pg_index x
    JOIN pg_class i ON i.oid = x.indexrelid
    JOIN pg_am am ON am.oid = i.relam
    WHERE am.amname = 'ybhnsw'
) v ON v.indrelid = c.oid
WHERE c.relkind IN ('r', 'p', 'm')
  AND EXISTS (
    SELECT 1
    FROM pg_attribute a
    JOIN pg_type t ON t.oid = a.atttypid
    WHERE a.attrelid = c.oid AND a.attnum > 0 AND NOT a.attisdropped
      AND t.typname IN ('vector', 'halfvec', 'sparsevec'))
ORDER BY 1, 2, 3;
```

The output lists a partitioned table and each of its partitions separately; rebuild them together, from the partitioned table's DDL. Materialized views also appear; drop and recreate them after their source tables.

Use the following table to decide what to do for each table.

| The vector table was created on | What to do |
| :--- | :--- |
| v2025.2.6.0+<br>v2026.1.1.0+<br>after the upgrade to that release was finalized | Nothing. Upgrade as usual. |
| v2024.2 | Save the DDL, export the data, and drop the tables, then drop the vector extension, before upgrading. See [Upgrade from v2024.2](#upgrade-from-v2024-2). |
| v2025.1, or v2025.2.0.x | Use [Option 1](#option-1-rebuild-across-the-upgrade): drop the vector tables before upgrading. Current releases can't open vector indexes created on these releases, so the upgrade can't proceed while they exist. |
| v2025.2.1 to v2025.2.5, or v2026.1.0.x | Use [Option 1](#option-1-rebuild-across-the-upgrade) or [Option 2](#option-2-rebuild-after-the-upgrade). Option 2 keeps the tables online during the upgrade. |
| Not sure | Treat it as old and rebuild it. Rebuilding a table that already has the new layout does no harm. |

If you plan to add a vector column to an existing table that was created before v2025.2.6.0 or v2026.1.1.0, recreate the table with the vector column instead of using `ALTER TABLE ... ADD COLUMN`.

{{< warning title="If an upgrade stops after the first node" >}}

If you upgrade while vector indexes created on v2025.1 or v2025.2.0.x still exist, the first upgraded node can't open them and the rolling upgrade stops. Roll back using a non-rolling rollback (this restarts all nodes at once and causes brief downtime), drop those tables as described in [Option 1](#option-1-rebuild-across-the-upgrade), and upgrade again.

{{< /warning >}}

{{< note title="Why a rebuild is needed" >}}

For every vector, YugabyteDB keeps a lookup entry that points from the vector back to the row it belongs to, so that a vector search can return rows. Starting with v2025.2.6.0 and v2026.1.1.0, these lookup entries belong to the table rather than to each vector index, and vector values are stored in a slightly different form.

Which layout a table uses is decided once, when the table is created with CREATE TABLE, and can't be changed afterwards. This has the following consequences:

- Old-layout tables aren't supported on the new releases. Tables created on v2025.2.1 to v2025.2.5 or v2026.1.0.x still return correct results, but every CREATE INDEX or DROP INDEX on them leaves lookup entries behind that are never cleaned up, so they waste storage indefinitely.
- Only a new CREATE TABLE gives a table the new layout. REINDEX, dropping and recreating only the index, ALTER TABLE, restoring from a backup, point-in-time recovery, and database cloning all keep the table's existing layout. That's why the procedures recreate each table from its DDL and reload its data.
- The new layout takes effect only after the upgrade is finalized. A table created while the upgrade is still in progress gets the old layout. Don't recreate vector tables until you have finalized the upgrade.
- In a colocated database, all colocated tables share storage, so space already lost isn't reclaimed by recreating individual tables. See [Colocated databases](#colocated-databases).
{{< /note >}}

### Upgrade from v2024.2

v2024.2 includes the `vector` extension as a tech preview, without vector index support. Tables that use the extension can't be upgraded in place: the [YSQL major upgrade](../../../manage/ysql-major-upgrade-yugabyted/) from PostgreSQL 11 to PostgreSQL 15 fails while the `vector` extension is installed, and pgvector changes from version 0.4.4 to 0.8.0 with no update path.

1. Before upgrading, save the DDL of the tables that use the `vector` type (for example, using `ysql_dump --schema-only`), and export their data (for example, using `\copy <table> TO '<table>.csv' WITH (FORMAT csv)`).
1. Drop those tables, and run `DROP EXTENSION vector;` in each database where the extension is installed.
1. Upgrade to the latest release in your series (no earlier than v2025.2.6.0 on the v2025.2 LTS series, or v2026.1.2.0 on the v2026.1 series), and finalize the upgrade.
1. Run `CREATE EXTENSION vector;`, recreate the tables, and create any vector indexes you need using `ybhnsw`.
1. Load the exported data.

Creating the vector indexes before loading the data avoids an index backfill.

### Upgrade from v2025.1, v2025.2, or v2026.1.0

Rebuild each vector table using one of the following options. If you are upgrading from v2025.1 or v2025.2.0.x, you must use Option 1.

#### Option 1: Rebuild across the upgrade

Use this option if the vector tables can be offline for the duration of the upgrade.

1. Before upgrading, save the DDL of each vector table and its indexes, and export the table data.
1. Drop the vector indexes, then drop the tables.
1. Upgrade to the latest release in your series (no earlier than v2025.2.6.0 on the v2025.2 LTS series, or v2026.1.2.0 on the v2026.1 series), and finalize the upgrade.
1. Recreate each table from its DDL, create the vector indexes, and then load the exported data.

#### Option 2: Rebuild after the upgrade

Use this option to keep the tables available during the upgrade. It applies only to vector tables created on v2025.2.1 to v2025.2.5 or v2026.1.0.x. For those tables, nothing is required before upgrading. After the upgrade is finalized, do the following for each vector table. Stop writes to the table from the start of the copy (step 3) until the tables are swapped (step 6). Rows written to the old table in that window are lost.

1. Drop the vector indexes on the table.
1. Create a new empty table, such as `items_new`, using the original DDL under the new name (for example, from `ysql_dump --schema-only -t items`). Don't create the vector indexes yet.
1. Copy the data, using `INSERT INTO items_new SELECT * FROM items;`. If the table has a `GENERATED ALWAYS AS IDENTITY` column, use `INSERT INTO items_new OVERRIDING SYSTEM VALUE SELECT * FROM items;`.
1. Create the vector indexes on the new table.
1. Save the definitions of the objects that depend on the old table, such as views and foreign keys in other tables that reference it, and then drop them. Renaming a table doesn't move these objects to the new table, and they would block dropping the old one.
1. Swap the tables, fix the sequence for any serial or identity column, and drop the old table:

    ```sql
    ALTER TABLE items RENAME TO items_old;
    ALTER TABLE items_new RENAME TO items;

    -- If the new table reuses the original sequence (as ysql_dump output does),
    -- move its ownership so that dropping items_old doesn't drop it.
    ALTER SEQUENCE items_id_seq OWNED BY items.id;

    -- If the new table created its own sequence (for example, from a serial or
    -- identity column), advance it past the copied values.
    SELECT setval(pg_get_serial_sequence('items', 'id'), (SELECT max(id) FROM items));

    DROP TABLE items_old;
    ```

1. Recreate the views and foreign keys that you dropped in step 5, and any grants or triggers that weren't part of the DDL you used in step 2.

Alternatively, you can create and populate the new table in one step using `CREATE TABLE items_new AS TABLE items;`. This form doesn't copy constraints, defaults, grants, or other indexes, so you must add them before swapping the tables.

### Colocated databases

In a [colocated](../../colocation/) database, all colocated tables share one tablet, and the reverse-mapping data left behind by tables in the old format stays in that tablet. Recreating a table switches it to the new format and stops further leaks, but doesn't reclaim the space already used; only dropping the database does. To reclaim the space, rebuild the whole database instead of individual tables: export it using `ysql_dump`, drop the database, and after the upgrade is finalized, create the database again and load the dump.

## Learn more

- Tutorial: [Build and Learn](/stable/develop/tutorials/build-and-learn/)
- Tutorials: [Build scalable generative AI applications with YugabyteDB](/stable/develop/ai/)
- [PostgreSQL pgvector: Getting Started and Scaling](https://www.yugabyte.com/blog/postgresql-pgvector-getting-started/)
- [Multimodal Search with PostgreSQL pgvector](https://www.yugabyte.com/blog/postgresql-pgvector-multimodal-search/)
