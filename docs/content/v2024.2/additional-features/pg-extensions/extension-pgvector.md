---
title: pgvector extension
headerTitle: pgvector extension
linkTitle: pgvector
description: Using the pgvector extension in YugabyteDB
tags:
  feature: tech-preview
menu:
  v2024.2:
    identifier: extension-pgvector
    parent: pg-extensions
    weight: 20
type: docs
---

The [pgvector](https://github.com/pgvector/pgvector) PostgreSQL extension allows you to store and query vectors, for use in performing similarity searches.

{{< note title="pgvector in YugabyteDB v2024.2" >}}
YugabyteDB v2024.2 supports the pgvector extension for storing and querying vectors using exact (sequential scan) similarity search only. Vector indexing is not available in this release.

CDC, backup/restore, and xCluster behavior with vector columns has not been formally QA-tested in this release.
{{< /note >}}

To enable the extension:

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

## Upgrade considerations

A database with the `vector` extension installed cannot be upgraded in place to v2025.1 or later. Export the data before dropping anything.

### OID conflict

In v2024.2 the `vector` type receives its object identifier (OID) when the extension is created, and that OID differs by cluster. v2025.1 and later use a fixed OID. pgvector also changes from 0.4.4 to 0.8.0 with no update path, and the PostgreSQL 11 to 15 upgrade fails while the extension is installed.

To upgrade, do the following:

1. Save the DDL of every table that uses the `vector` type (for example, with `ysql_dump --schema-only`) and export the data (for example, with `\copy <table> TO '<table>.csv' WITH (FORMAT csv)`).
1. Drop those tables, then run `DROP EXTENSION vector;` in each database where the extension is installed.
1. Upgrade and finalize.
1. Run `CREATE EXTENSION vector;`, recreate the tables, and load the exported data.

### Vector indexes after the upgrade

v2024.2 has no vector indexes. v2025.1 stores a per-row identifier that vector indexes require, and rows written in v2024.2 do not have it. A table kept across the upgrade cannot be indexed. Recreating the tables and loading the data, as in the preceding steps, writes the rows in the new format.

## Read more

- Tutorial: [Build and Learn](/stable/develop/tutorials/build-and-learn/)
- Tutorials: [Build scalable generative AI applications with YugabyteDB](/stable/develop/ai/)
- [PostgreSQL pgvector: Getting Started and Scaling](https://www.yugabyte.com/blog/postgresql-pgvector-getting-started/)
- [Multimodal Search with PostgreSQL pgvector](https://www.yugabyte.com/blog/postgresql-pgvector-multimodal-search/)