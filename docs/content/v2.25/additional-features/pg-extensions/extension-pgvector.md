---
title: pgvector extension
headerTitle: pgvector extension
linkTitle: pgvector
description: Using the pgvector extension in YugabyteDB
menu:
  v2.25:
    identifier: extension-pgvector
    parent: pg-extensions
    weight: 20
type: docs
---

The [pgvector](https://github.com/pgvector/pgvector) PostgreSQL extension allows you to store and query vectors, for use in performing similarity searches.

Vector distance functions measure similarity or difference between high-dimensional data points. Choosing the right function depends on the use case, such as search, ranking, or clustering. YugabyteDB supports the following distance functions:

- Cosine Distance - Measures the angle between two vectors. Used for comparing direction rather than magnitude. Best for text similarity and recommendation systems.
- L2 (Euclidean) Distance - Measures the straight-line distance between two points in space. Best when absolute differences in values matter, like in image recognition.
- Inner Product - Measures similarity by multiplying corresponding elements and summing them. Often used in ranking and recommendation models, where larger values indicate higher similarity.

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

YugabyteDB currently supports the `vector` type.

#### HNSW index options

You can fine-tune HNSW indexing using the following parameters:

- `m` - specifies the maximum number of connections per layer.
- `ef_construction` - Specifies the size of the dynamic candidate list for constructing the graph.

For example:

```sql
CREATE INDEX NONCONCURRENTLY ON items USING ybhnsw (embedding vector_l2_ops) WITH (m = 16, ef_construction = 128);
```

A higher `ef_construction` value provides faster recall at the cost of index build time / insert speed.

### Limitations

- Concurrent index creation is not supported yet.
- Partial indexes on vector columns are not supported yet.
- Vector indexes are not supported for [xCluster replication](../../../architecture/docdb-replication/async-replication/).

## Learn more

- Tutorial: [Build and Learn](/stable/develop/tutorials/build-and-learn/)
- Tutorials: [Build scalable generative AI applications with YugabyteDB](/stable/develop/ai/)
- [PostgreSQL pgvector: Getting Started and Scaling](https://www.yugabyte.com/blog/postgresql-pgvector-getting-started/)
- [Multimodal Search with PostgreSQL pgvector](https://www.yugabyte.com/blog/postgresql-pgvector-multimodal-search/)
