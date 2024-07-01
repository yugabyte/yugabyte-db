---
title: Configurable data sharding
headerTitle: Configurable data sharding
linkTitle: Configurable data sharding
headcontent: Understand the various data distribution schemes
menu:
  stable:
    identifier: data-sharding
    parent: going-beyond-sql
    weight: 300
rightNav:
  hideH3: true
type: docs
---

YugabyteDB splits table data into smaller pieces called [tablets](../../../architecture/key-concepts/#tablet).
Sharding is the process in which rows of a table are distributed across the various tablets in the cluster. The mapping of a row to a tablet is deterministic and based on the primary key of the row. The deterministic nature of sharding enables fast access to a row for a given primary key. Sharding is one of the foundational techniques that enable the scalability of YugabyteDB.

YugabyteDB offers two schemes for data sharding, namely [Hash](#hash-sharding) and [Range](#range-sharding).

|                 Functionality                  |         Range sharding          |        Hash sharding        |
| :--------------------------------------------- | ------------------------------- | --------------------------- |
| Distribution of data based on                  | Actual value of the Primary key | Hash of the Primary key     |
| Ordering of data in a partition                | Actual value of the Primary key | Hash of the Primary key     |
| Location of contiguous keys (e.g., 1,2,3,4)    | Mostly on the same tablet       | Mostly on different tablets |
| Performance of point lookups `k=100`           | High                            | High                        |
| Performance of Range lookups `k>150 and k<180` | High                            | Low                         |
| Tablet splitting                               | Manual and automatic            | Automatic                   |

## Cluster setup

To understand how these sharding schemes work, set up a local RF 3 cluster for testing.

{{<setup/local>}}

## Range sharding

In range sharding the data is split into contiguous ranges of the primary key (in the sort order as defined in the table). Typically tables start as a single tablet and as the data grows and reaches a size threshold, the tablet dynamically splits into two. Tables can also be pre-split during creation.

The primary advantage of range sharding is that it is fast both for range scans and point lookups. The downside to this sharding scheme is that it starts with only one tablet even though there could be multiple nodes in the cluster. The other concern is that if the data being inserted is already sorted, all new inserts would go to only one tablet, even though there are multiple tablets in the cluster.

### Behavior

Consider a basic table of users for which the user ID is the primary key.

```sql
CREATE TABLE user_range (
    id int,
    name VARCHAR,
    PRIMARY KEY(id ASC)
);
```

{{<note>}}
The word **ASC** has been explicitly added to the primary key definition to specify that this is a range-sharded table. This defines the ordering of the rows in the tablet. You can specify **DESC** if you want the rows to be stored in descending order.
{{</note>}}

Add 6 rows to the table:

```sql
INSERT INTO user_range VALUES (1, 'John Wick'), (2, 'Harry Potter'), (3, 'Bruce Lee'),
                              (4, 'India Jones'), (5, 'Vito Corleone'), (6, 'Jack Sparrow');
```

Fetch all the rows from the table:

```sql
SELECT * FROM user_range ;
```

If you select all the rows from the table, you will see an output similar to the following:

```caddyfile{.nocopy}
 id |     name
----+---------------
  1 | John Wick
  2 | Harry Potter
  3 | Bruce Lee
  4 | India Jones
  5 | Vito Corleone
  6 | Jack Sparrow
```

Note that all the rows are ordered in ascending order of ID. This is because you specified `id ASC` in the primary key definition.

Even though you have set up a cluster with 3 nodes, only 1 tablet has been created by default. A range-sharded table cannot be split into multiple tablets automatically because the range of numbers is not fixed (it is actually infinite) and it is inefficient for the system to decide which range of numbers should be stored in which tablet. So the inserted rows will be stored only on this tablet.

To verify this, execute the same command with EXPLAIN ANALYZE as follows:

```sql
EXPLAIN (ANALYZE, DIST, COSTS OFF) SELECT * FROM user_range ;
```

You will see an output like the following:

```yaml{.nocopy}
                            QUERY PLAN
------------------------------------------------------------------
 Seq Scan on user_range (actual time=0.865..0.867 rows=6 loops=1)
...
 Storage Read Requests: 1
 Storage Rows Scanned: 6
...
 ```

The `Storage Rows Scanned: 6` metric clearly says that 6 rows were read and `rows=6` on the first line says that 6 rows were returned.

Now pay attention to the **`Storage Read Requests: 1`**. This says that there was just 1 read request to the storage subsystem. This is because there is only one tablet and all the rows are stored in it.

Now to see how point lookups perform in a range sharded table, fetch the row for a specific row:

```sql
explain (analyze, dist, costs off) select * from user_range where id=5;
```

You should see an output similar to the following:

```yaml{.nocopy}
                                       QUERY PLAN
------------------------------------------------------------------------------------------
 Index Scan using user_range_pkey on user_range (actual time=1.480..1.488 rows=1 loops=1)
   Index Cond: (id = 5)
...
 Storage Read Requests: 1
 Storage Rows Scanned: 1
...
```

This shows that only one row was scanned (via `Storage Rows Scanned: 1`) to retrieve one row (via `rows=1`), making point lookups very efficient in a range-sharded table.

To understand how range scans are efficient in a range-sharded table, fetch the rows with `id > 2 and id < 6` as follows:

```sql
explain (analyze, dist, costs off) select * from user_range where id>2 and id<6;
```

You should see an output similar to the following:

```yaml{.nocopy}
                                        QUERY PLAN
------------------------------------------------------------------------------------------
 Index Scan using user_range_pkey on user_range (actual time=1.843..1.849 rows=3 loops=1)
   Index Cond: ((id > 2) AND (id < 6))
...
 Storage Rows Scanned: 3
...
```

Exactly 3 rows were scanned (via `Storage Rows Scanned: 3`) to retrieve 3 rows. This is because the data is ordered on the primary key and hence an index scan was done for the range query (via `Index Scan using user_range_pkey`).

{{<tip title="Remember">}}
Range sharding is the preferred sharding scheme on YugabyteDB as it is good for both range queries and point lookups. In a range-shared table, data is ordered on the ordering of the primary key.
{{</tip>}}

## Hash sharding

In hash sharding, the data is randomly and evenly distributed across the tablets based on the hash of the primary key. Each tablet owns a range of hash values in the 2-byte hash space - `0x0000 - 0xFFFF`. For instance, in a 3-tablet table, the hash space could be split as `0x0000 - 0x5554`, `0x5555 - 0xAAA9`, `0xAAAA - 0xFFFF` with each tablet being responsible for one of the ranges.

In a hash-sharded table, even though the data is distributed well across all available tablets, in each tablet, the data is stored in the sorted order of the hash of the primary key. This leads to fast lookups but lower performance of range queries like greater than and lesser than (for example, `age < 20`).

### Behavior

Consider a basic table of users for which the user ID is the primary key.

```sql
CREATE TABLE user_hash (
    id int,
    name VARCHAR,
    PRIMARY KEY(id HASH)
);
```

{{<note>}}
For clarity, `HASH` has been explicitly added to the primary key definition to specify that this is a hash-sharded table. Hash sharding is the default distribution scheme. You can change the default using the `yb_use_hash_splitting_by_default` configuration parameter.
{{</note>}}

Add 6 rows to the table:

```sql
INSERT INTO user_hash VALUES (1, 'John Wick'), (2, 'Harry Potter'), (3, 'Bruce Lee'),
                             (4, 'India Jones'), (5, 'Vito Corleone'), (6, 'Jack Sparrow');
```

Fetch all the rows from the table:

```sql
SELECT * FROM user_hash ;
```

You will see an output like the following:

```caddyfile{.nocopy}
 id |     name
----+---------------
  5 | Vito Corleone
  1 | John Wick
  6 | Jack Sparrow
  4 | India Jones
  2 | Harry Potter
  3 | Bruce Lee
```

Note how there is no ordering of the primary key `id`. This is because the rows are internally ordered on the hash of the id, which can be determined using the function `yb_hash_code(id)`.

As you set up a cluster with 3 nodes, 3 tablets were created by default. The reason why the table can be split into multiple tablets easily in hash sharding is that the hash range is fixed `[0x0000 - 0xFFFF]`, exactly 65536 numbers, and hence it is straightforward to divide this range across different tablets. So the inserted rows would have been distributed across the 3 tablets.

To verify this, execute the same command with EXPLAIN ANALYZE as follows:

```sql
EXPLAIN (ANALYZE, DIST, COSTS OFF) SELECT * FROM user_hash ;
```

You will see an output like the following:

```yaml{.nocopy}
                           QUERY PLAN
-----------------------------------------------------------------
 Seq Scan on user_hash (actual time=0.526..2.054 rows=6 loops=1)
...
 Storage Read Requests: 3
 Storage Rows Scanned: 6
...
```

The `Storage Rows Scanned: 6` metric shows that 6 rows were read and `rows=6` on the first line says that 6 rows were returned. Now pay attention to the `Storage Read Requests: 3`. This says that there were 3 different read requests to the storage subsystem. This is because 3 different requests have been sent to the 3 different tablets to retrieve the rows.

To see how efficient point lookups are in a hash-sharded table, execute the following:

```sql
explain (analyze, dist, costs off) select * from user_hash where id=5;
```

You will see an output similar to the following:

```yaml{.nocopy}
                                       QUERY PLAN
----------------------------------------------------------------------------------------
 Index Scan using user_hash_pkey on user_hash (actual time=1.479..1.487 rows=1 loops=1)
   Index Cond: (id = 5)
...
 Storage Read Requests: 1
 Storage Rows Scanned: 1
...
```

The primary key index, `user_hash_pkey`, was used to look up `id = 5` and only one row was scanned to retrieve one row. This shows that point lookups are very efficient on a hash-sharded table.

To see the performance of a range scan on a hash-sharded table, fetch the rows with `id > 2 and id < 6` as follows:

```sql
explain (analyze, dist, costs off) select * from user_hash where id>2 and id<6;
```

You should see an output similar to the following:

```yaml{.nocopy}
                           QUERY PLAN
-----------------------------------------------------------------
 Seq Scan on user_hash (actual time=2.535..2.547 rows=3 loops=1)
   Remote Filter: ((id > 2) AND (id < 6))
...
 Storage Rows Scanned: 6
...
```

You will immediately notice that this was a sequential scan (via `Seq Scan on user_hash`) and all 6 rows in the table were scanned (via `Storage Rows Scanned: 6`) to retrieve 3 rows. If there were a million rows in the table, all of them would have been scanned. Hence, hash-sharded tables don't perform well for range queries.

{{<tip title="Remember">}}
Hash sharding is good for point lookups. In a hash-sharded table, data is evenly distributed and is ordered on the ordering of the hash of the primary key.
{{</tip>}}

## Learn more

- [The internals of sharding](../../../architecture/docdb-sharding/sharding)
- [Data distribution](../../linear-scalability/data-distribution)
