---
title: Bucket indexes in YugabyteDB YSQL
headerTitle: Bucket indexes
linkTitle: Bucket indexes
description: Using bucket indexes in YSQL
headContent: Explore bucket indexes in YugabyteDB using YSQL
menu:
  stable:
    identifier: bucket-index-ysql
    parent: explore-indexes-constraints-ysql
    weight: 250
tags:
  feature: tech-preview
type: docs
---

Traditional leading range (ASC/DESC) indexes with monotonic inserts concentrate all writes on the "most recent" tablet, creating hot shards and uneven resource usage. By prepending a bucket column (often a hash code modulo of a key column) to the ASC/DESC key, writes can be evenly distributed across multiple tablets (buckets), achieving write scalability and balanced resource usage.

The goal is to have a globally ordered result (for example, the latest 1000 rows by timestamp) while avoiding write hot spots on a monotonically increasing column. The bucket-based index solves the hot spot issue by distributing data across multiple tablets (buckets). Using the `yb_enable_derived_saops=true` planner optimization (available in v2025.2.1.0 and later), the system can return a globally ordered result for range queries and LIMIT clauses without a quicksort operation, even though the data is physically sharded by the bucket. This makes queries like top-N and keyset pagination very efficient.

Normally, to get a globally ordered result for the most recent 1000 rows by timestamp (for example) from data that is sharded across multiple tablets, the database would have to:

1. Query each tablet.
1. Collect all of the results into one place.
1. Perform a final global sort to ensure the total result set is correctly ordered, even with a LIMIT clause.

This is resource-intensive and slow.

Using a bucket index with the Limit pushdown optimization, the database can to "push down" the LIMIT request to each of the individual tablets (buckets).

For a timestamp column that is the second column in the index (and ordered ASC), each tablet can quickly find its top 1000 locally ordered rows. The database only has to scan 1000 rows per bucket instead of scanning potentially millions of rows that match the larger range condition.

YugabyteDB then performs an efficient merge of the small, pre-sorted result sets from each bucket to produce the final globally ordered 1000 rows, without a final global sort.

In short, it achieves the necessary write scalability and global ordering simultaneously, making OLTP top-N queries exceptionally fast.

## Syntax

```sql
CREATE INDEX index_name ON table_name(yb_hash_code("key_columns") % <modulo>) ASC, column_name asc) 
SPLIT AT VALUES ((1), (2));
```

The simplest way to perform modulo sizing is based on your write throughput; if you have 3 nodes, you would set a modulo of 3 to have it write to all 3. If you had 9 nodes and received high write-throughput, then the recommended modulo would be at least 9.

For a unique index or primary key, the arguments to `yb_hash_code` must be a subset of the unique index key or primary key columns. If it's a non-unique index, the columns in the `yb_hash_code` don't matter as long as they are deterministic.

The SPLIT clause is optional but recommended, as it provides even distribution of each bucket across nodes.

Bucket column or index expression column should generally be the first column of the key.

## Example

Follow the [setup instructions](../../cluster-setup-local/) to start a local multi-node universe with a replication factor of 3. You must be running v2025.2.1.0 or later.

Create a table with a monotonic column:

```sql
CREATE TABLE te (
    id SERIAL PRIMARY KEY,
    "timestamp" TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

Create an index which always writes to the 3 nodes:

```sql
CREATE INDEX yb_nothotspot
    ON te ((yb_hash_code("timestamp") % 3) ASC, "timestamp") include (id)
    SPLIT AT VALUES ((1), (2));
```

As a reminder, an ordinary "hotspot" index would have used the following:

```sql
ON te ("timestamp" asc) include (id);
```

Insert monotonic data:

```sql
INSERT INTO te ("timestamp")
SELECT
    '2026-01-01 00:00:00+00'::timestamptz
    + (gs * (('2024-01-01 00:00:00+00'::timestamptz - '2026-01-01 00:00:00+00'::timestamptz) / 10000000))
FROM generate_series(0, 9999999) AS gs;
```

### Using the bucket index

Enable features that preserve global ordering across the buckets by activating planner optimisations:

```sql
analyze;
set yb_max_saop_merge_streams=64;
set yb_enable_derived_saops=true;
set yb_enable_cbo=on;
Alter database yugabyte set yb_max_saop_merge_streams=64;
Alter database yugabyte set yb_enable_derived_saops=true;
Alter database yugabyte set yb_enable_cbo=on;
```

Observe planner changes that provide global ordering with no SQL or application changes:

```sql
explain (Analyze)
    SELECT timestamp
    FROM te
    WHERE "timestamp" >= '2020-01-01'
        AND "timestamp" <  '2030-01-01'
    ORDER BY timestamp;
```

```output
 Index Only Scan using yb_nothotspot on te (actual rows=10000000 loops=1)
   Index Cond: (("timestamp" >= '2020-01-01 00:00:00+00'::timestamp with time zone) AND ("timestamp" < '2030-01-01 00:00:00+00'::timestamp with time zone) AND (((yb_hash_code("timestamp") % 3)) = ANY ('{0,1,2}'::integer[])))
   Merge Sort Key: "timestamp"
   Merge Stream Key: (yb_hash_code("timestamp") % 3)
   Merge Streams: 3
```

Notice that no sort is present and that the `yb_enable_derived_saops` feature passed the "bucket" column into the index condition. YugabyteDB has then merged the streams and returned the data without a sort node.

### Add a LIMIT

You really see how powerful this feature is when you use the LIMIT clause.

```sql
explain (analyse, costs off, timing on, dist)
    SELECT timestamp
    FROM te
    WHERE "timestamp" >= '2020-01-01'
    AND "timestamp" <  '2030-01-01'
    ORDER BY timestamp
    LIMIT 1000;
```

```output
 Limit (actual time=1.682..2.258 rows=1000 loops=1)
   ->  Index Only Scan using yb_nothotspot on te (actual time=1.680..2.122 rows=1000 loops=1)
         Index Cond: (("timestamp" >= '2020-01-01 00:00:00+00'::timestamp with time zone) AND ("timestamp" < '2030-01-01 00:00:00+00'::timestamp with time zone) AND (((yb_hash_code("timestamp") % 3)) = ANY ('{0,1,2}'::integer[])))
         Merge Sort Key: "timestamp"
         Merge Stream Key: (yb_hash_code("timestamp") % 3)
         Merge Streams: 3
         Storage Index Rows Scanned: 3000
 Execution Time: 2.402 ms
```

The SQL is asking for 1000 globally ordered rows, and YugabyteDB can again return it without the sort node, while scanning 1000 rows per bucket. This is very efficient.

This also works perfectly for more complex OLTP top-n queries, such as keyset pagination.

Create a more complicated index and an extra predicate:

```sql
ALTER TABLE te ADD COLUMN key_id integer NOT NULL DEFAULT 123;
analyze;
CREATE INDEX scalable_key_timestamp ON public.te (
    (yb_hash_code("timestamp") % 3) asc,
    key_id,
    "timestamp" ASC,
    id
) SPLIT AT VALUES ((1), (2));
```

```sql
explain (analyze, costs off, timing on)
SELECT *
    FROM public.te
    WHERE 1=1
        AND key_id = 123
        AND "timestamp" >= '2025-05-05 08:00:00'
        AND ("timestamp", id) > ('2025-05-05 08:00:00', 1)
    ORDER BY "timestamp" ASC, id ASC
    LIMIT 1000;
```

```output
 Limit (actual time=2.059..2.633 rows=1000 loops=1)
   ->  Index Only Scan using scalable_key_timestamp on te (actual time=2.058..2.513 rows=1000 loops=1)
         Index Cond: ((key_id = 123) AND ("timestamp" >= '2025-05-05 08:00:00+00'::timestamp with time zone) AND (ROW("timestamp", id) > ROW('2025-05-05 08:00:00+00'::timestamp with time zone, 1)) AND (((yb_hash_code("timestamp") % 3)) = ANY ('{0,1,2}'::integer[])))
         Merge Sort Key: "timestamp", id
         Merge Stream Key: (yb_hash_code("timestamp") % 3)
         Merge Streams: 3
         Heap Fetches: 0
 Planning Time: 0.139 ms
 Execution Time: 2.777 ms
 Peak Memory Usage: 24 kB
```

The global ordering is still preserved on this keyset pagination without any changes to the SQL.
