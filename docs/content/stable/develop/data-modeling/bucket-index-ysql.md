---
title: Bucket-based indexes in YugabyteDB YSQL
headerTitle: Bucket-based indexes
linkTitle: Bucket-based indexes
description: Using bucket-based indexes in YSQL
headContent: Use bucket-based distribution of indexes to avoid hotspots
menu:
  stable_develop:
    identifier: bucket-index-ysql
    parent: data-modeling
    weight: 310
tags:
  other: ysql
  feature: tech-preview
type: docs
---

Traditional leading range (ASC/DESC) indexes with monotonic inserts concentrate all writes on the "most recent" tablet, creating [hot shards](../hot-shards-ysql/) and uneven resource usage. By prepending a bucket column (often a hash code modulo of a key column) to the ASC/DESC key, writes can be evenly distributed across multiple tablets (buckets), achieving write scalability and balanced resource usage.

The goal is to have a globally ordered result (for example, the latest 1000 rows by timestamp) while avoiding write hot spots on a monotonically increasing column. The bucket-based index solves the hot spot issue by distributing data across multiple tablets (buckets). Using the `yb_enable_derived_saops=true` planner optimization (available in v2025.2.1.0 and later), the system can return a globally ordered result for range queries and LIMIT clauses without a sort operation, even though the data is physically sharded by the bucket. This makes queries like top-N and keyset pagination very efficient.

Normally, to get a globally ordered result for the most recent 1000 rows by timestamp (for example) from data that is sharded across multiple tablets, the database would have to:

1. Query each tablet.
1. Collect all of the results into one place.
1. Perform a final global sort to ensure the total result set is correctly ordered, even with a LIMIT clause.

This is resource-intensive and slow.

Using a bucket-based index with the Limit pushdown optimization, the database can "push down" the LIMIT request to each of the individual tablets (buckets).

For example, for a timestamp column that is the second column in the index (and ordered ASC), for each bucket, we can quickly find its top 1000 locally ordered rows. The database only has to scan 1000 rows per bucket instead of scanning potentially millions of rows that match the larger range condition.

YugabyteDB then performs an efficient merge of the small, pre-sorted result sets from each bucket to produce the final globally ordered 1000 rows without a final global sort.

In short, it achieves the necessary write scalability and global ordering simultaneously, making OLTP top-N queries exceptionally fast.

## When to use it

Use bucket-based indexes for the following workloads:

- Timestamp-ordered inserts.
- Sequence-based IDs.
- "Latest N items" queries (feeds, time-series, audit tables).

For example:

- ASC/DESC indexed columns with a high write throughput of monotonic values (timestamps or sequences).
- ASC/DESC indexed columns requiring range and/or ordered SQL access without hash equality (latest N items queries).
- Indexed columns that are ASC/DESC sharded, and the first high-cardinality column in the key (which could be the second or third from the first key position) is monotonically increasing.

## Syntax

```sql
CREATE INDEX index_name ON table_name(yb_hash_code(<key_columns>) % <buckets>) ASC, column_name ASC) 
SPLIT AT VALUES ((1), (2));
```

- For unique indexes, columns in [yb_hash_code](../../../api/ysql/exprs/func_yb_hash_code/) must be a subset of the remaining columns in the key. Non-unique indexes can have anything in `yb_hash_code`.

- The bucket column or index expression column should generally be the first column of the key.

- What you set the number of buckets to depends on how much you want to spread the previously-hot-shard load. The simplest recommendation is to make it equal to the number of nodes.

- You should take care that the load is spread to other nodes. For example, with 3 nodes and 9 tablets, if there are 3 buckets, each bucket having 3 tablets, it could be the case that the hottest tablet of each bucket is on the same node.

    To reduce the chance of this happening, increase number of buckets and/or keep tablet-to-bucket ratio low (or increase number of nodes).

- You can't change the number of buckets after creation (not an issue for secondary indexes, which can be recreated).

- The SPLIT clause is optional but recommended to cleanly distribute each bucket to its own tablet. For example, with 3 buckets, if tablets were dynamically split to ((0, 99999), (1, 99999)), then the scan for buckets 0 and 1 may query two tablets. If tablets were presplit to ((1), (2)), all 3 buckets would query one tablet.

## Parameters

You configure bucket-based indexing in the [query planner](../../../architecture/query-layer/planner-optimizer/) using the following configuration parameters:

- yb_enable_derived_equalities: Set to `true`.
- yb_enable_derived_saops: Set to `true`.
- yb_max_saop_merge_streams: Maximum number of buckets to process in parallel. The recommended value is 64.

In addition, the [cost-based optimizer](../../../best-practices-operations/ysql-yb-enable-cbo/) must be enabled (the default).

## Example

The following example walks through using a bucket-based index to avoid write hot spots on a timestamp column. You create a table, define an index that distributes writes across three buckets (tablets), insert sample rows with timestamps over a range, and enable the planner settings for bucket-based merge. Then you run queries with ORDER BY and LIMIT and confirm in the plan that there is no Sort node—ordering comes from merging streams from each bucket.

Follow the [setup instructions](../../../explore/cluster-setup-local/#multi-node-universe) to start a local multi-node universe with a replication factor of 3, and connect to universe using ysqlsh. You must be running v2025.2.1.0 or later.

Create a table with a monotonic column:

```sql
CREATE TABLE te (
    id SERIAL PRIMARY KEY,
    "timestamp" TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

Create an index that always writes to the 3 nodes by adding a bucket column and SPLIT AT:

```sql
CREATE INDEX yb_nothotspot
    ON te ((yb_hash_code(timestamp) % 3) ASC, timestamp) INCLUDE (id)
    SPLIT AT VALUES ((1), (2));
```

SPLIT AT ensures each bucket (0, 1, 2) maps to its own tablet, so writes are spread across three tablets.

As a reminder, an ordinary "hot-spot" index would use only the timestamp column (no bucket column and no SPLIT AT).

```output.sql
ON te ("timestamp" ASC) INCLUDE (id);
```

Insert rows with increasing timestamps:

```sql
INSERT INTO te (timestamp)
SELECT
    '2026-01-01 00:00:00+00'::timestamptz
    + (gs * (('2024-01-01 00:00:00+00'::timestamptz - '2026-01-01 00:00:00+00'::timestamptz) / 100000))
FROM generate_series(0, 99999) AS gs;
```

### Configure bucket-based indexing

Enable features that preserve global ordering across the buckets by setting the configuration parameters:

```sql
ANALYZE;
SET yb_max_saop_merge_streams=64;
SET yb_enable_derived_saops=true;
SET yb_enable_derived_equalities=true;
SET yb_enable_cbo=on;
ALTER DATABASE yugabyte SET yb_max_saop_merge_streams=64;
ALTER DATABASE yugabyte SET yb_enable_derived_saops=true;
ALTER DATABASE yugabyte SET yb_enable_derived_equalities=true;
ALTER DATABASE yugabyte SET yb_enable_cbo=on;
```

Observe planner changes that provide global ordering with no SQL or application changes:

```sql
EXPLAIN (ANALYZE, COSTS off)
    SELECT timestamp
    FROM te
    WHERE "timestamp" >= '2020-01-01'
        AND "timestamp" <  '2030-01-01'
    ORDER BY timestamp;
```

```output
 Index Only Scan using yb_nothotspot on te  (cost=27.57..4611.19 rows=99980 width=8) (actual time=2.259..65.434 rows=100000 loops=1)
   Index Cond: (("timestamp" >= '2020-01-01 00:00:00-05'::timestamp with time zone) AND ("timestamp" < '2030-01-01 00:00:00-05'::timestamp with time zone) AND (((yb_hash_code("timestamp") % 3)) = ANY ('{0,1,2}'::integer[])))
   Merge Sort Key: "timestamp"
   Merge Stream Key: (yb_hash_code("timestamp") % 3)
   Merge Streams: 3
```

- Merge Sort Key: columns that the index uses for the merge.
- Merge Stream Key: columns involved in forming buckets.
- Merge Streams: number of streams that the index merges, generally the cardinality of the cross product of all merge stream key values.
- Merge Stream Key: columns involved in forming buckets (does not include columns fixed to constants).
- Merge Streams: number of streams that YugabyteDB merges, generally the cross product of all merge stream key values.

Notice that no sort is present and that the `yb_enable_derived_saops` feature passed the "bucket" column into the index condition. The index has then merged the streams and returned sorted data, eliminating the need for a sort.

### Add a LIMIT

You really see how powerful this feature is when you use the LIMIT clause.

First turn off the optimization:

```sql
SET yb_enable_derived_saops = false;
SET yb_enable_derived_equalities = false;
SET yb_max_saop_merge_streams = 0;
```

Run the following query:

```sql
EXPLAIN (ANALYZE, COSTS off, TIMING on)
    SELECT timestamp
    FROM te
    WHERE "timestamp" >= '2020-01-01'
    AND "timestamp" <  '2030-01-01'
    ORDER BY timestamp
    LIMIT 1000;
```

```output
 Limit (actual time=94.600..94.975 rows=1000 loops=1)
   ->  Sort (actual time=94.596..94.778 rows=1000 loops=1)
         Sort Key: "timestamp"
         Sort Method: top-N heapsort  Memory: 49kB
         ->  Index Only Scan using yb_nothotspot on te (actual time=5.413..64.657 rows=100000 loops=1)
               Index Cond: (("timestamp" >= '2020-01-01 00:00:00-05'::timestamp with time zone) AND ("timestamp" < '2030-01-01 00:00:00-05'::timestamp with time zone))
               Heap Fetches: 0
 Planning Time: 0.494 ms
 Execution Time: 95.281 ms
 Peak Memory Usage: 117 kB
```

Without the optimization, the planner performs a sort as expected.

Turn the optimization back on:

```sql
SET yb_enable_derived_saops = true; 
SET yb_enable_derived_equalities = true; 
SET yb_max_saop_merge_streams = 64; 
```

Run the query again:

```sql
EXPLAIN (ANALYZE, COSTS off, TIMING on)
    SELECT timestamp
    FROM te
    WHERE "timestamp" >= '2020-01-01'
    AND "timestamp" <  '2030-01-01'
    ORDER BY timestamp
    LIMIT 1000;
```

```output
 Limit (actual time=4.783..6.583 rows=1000 loops=1)
   ->  Index Only Scan using yb_nothotspot on te (actual time=1.680..2.122 rows=1000 loops=1)
         Index Cond: (("timestamp" >= '2020-01-01 00:00:00+00'::timestamp with time zone) AND ("timestamp" < '2030-01-01 00:00:00+00'::timestamp with time zone) AND (((yb_hash_code("timestamp") % 3)) = ANY ('{0,1,2}'::integer[])))
         Merge Sort Key: "timestamp"
         Merge Stream Key: (yb_hash_code("timestamp") % 3)
         Merge Streams: 3
         Heap Fetches: 0
 Planning Time: 0.456 ms
 Execution Time: 7.241 ms
 Peak Memory Usage: 8 kB
```

The SQL is asking for 1000 globally ordered rows, and YugabyteDB again returns it, without the sort, while scanning 1000 rows per bucket, in a fraction of the time.

The bucket-based index also works well for more complex OLTP top-N queries, such as keyset pagination.

Create a more complicated index and an extra predicate:

```sql
ALTER TABLE te ADD COLUMN key_id integer NOT NULL DEFAULT 123;
ANALYZE;
CREATE INDEX scalable_key_timestamp ON te (
    (yb_hash_code("timestamp") % 3) ASC,
    key_id,
    "timestamp" ASC,
    id
) SPLIT AT VALUES ((1), (2));
```

Run the following query:

```sql
EXPLAIN (ANALYZE, COSTS off, TIMING on)
SELECT *
    FROM te
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

### Point lookups

Single-row lookups by exact key work with bucket-based indexes because the planner automatically adds the bucket predicate to the index condition. As a result, the query can target the correct tablet instead of scanning all buckets.

Using the primary key (id, timestamp), the `bucket_id` clause is automatically added:

```sql
EXPLAIN
    SELECT *
    FROM te
    WHERE id = 1
    AND "timestamp" = '2025-01-01 00:00:00+00'::TIMESTAMPTZ;
```

```output
 Index Scan using te_pkey on te
 Index Cond: ((bucket_id = (yb_hash_code(1, '2025-01-01 00:00:00+00'::timestamptz) % 3)) 
    AND (id = 1) 
    AND ("timestamp" = '2025-01-01 00:00:00+00'::timestamptz))
```

Create a secondary index named `te_key_id_secondary`. This index uses a bucket key on the `key_id` column to distribute writes evenly while still allowing for efficient lookups on `key_id` and `id`.

```sql
CREATE INDEX te_key_id_secondary ON te (
    (yb_hash_code(key_id) % 3) ASC,
    key_id,
    id
) SPLIT AT VALUES ((1), (2));
```

Run an EXPLAIN on a point lookup query.

```sql
EXPLAIN SELECT * FROM te WHERE key_id = 123 AND id = 1;
```

```output
Index Scan using te_key_id_secondary on te
Index Cond: (((yb_hash_code(key_id) % 3) = (yb_hash_code(123) % 3)) AND (key_id = 123) AND (id = 1))
```

The query planner automatically calculates the bucket ID for the search keys (`key_id = 123` and `id = 1`) and adds the bucket predicate to the index condition. This allows the database to go directly to a single, specific tablet (bucket).

## Learn more

- [Hot shards](../hot-shards-ysql/)
- [Scaling writes](../../../explore/linear-scalability/scaling-writes)
- [Cost-based optimizer (CBO)](../../../best-practices-operations/ysql-yb-enable-cbo)
- [EXPLAIN and ANALYZE](../../../launch-and-manage/monitor-and-alert/query-tuning/explain-analyze)
