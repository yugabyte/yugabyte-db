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
type: docs
---

The [pgvector](https://github.com/pgvector/pgvector) PostgreSQL extension allows you to store and query vectors, for use in performing similarity searches.

Note: YugabyteDB support for pgvector does not currently include indexing.

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

Note: Combine with `ORDER BY` and `LIMIT` to use an index.

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

## Read more

- [Build scalable generative AI applications with Azure OpenAI and YugabyteDB](../../../../tutorials/azure/azure-openai/)
