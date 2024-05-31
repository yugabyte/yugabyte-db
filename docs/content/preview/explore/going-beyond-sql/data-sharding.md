---
title: Configurable data sharding
headerTitle: Configurable data sharding
linkTitle: Configurable data sharding
headcontent: Understand the various data distribution schemes
menu:
  preview:
    identifier: data-sharding
    parent: going-beyond-sql
    weight: 300
rightNav:
  hideH3: true
type: docs
---

YugabyteDB splits table data into smaller pieces called [tablets](../../../architecture/key-concepts/#tablet).
Sharding is the process in which rows of a table are distributed across the various tablets in the cluster. The mapping of a row to a tablet is deterministic and based on the primary key of the row. The deterministic nature of sharding enables fast access to a row for a given primary key. Sharding is one of the foundational techniques that enable the scalability of YugabyteDB.

{{<note>}}
In the absence of an explicit primary key, YugabyteDB automatically inserts an internal `row_id` to be used as the primary key. This `row_id` is not accessible by users.
{{</note>}}

Let us look into the two sharding schemes offered by YugabyteDB.

## Cluster setup

To understand how these sharding schemes work, let's set up a local RF3 cluster for testing.

{{<setup/local>}}

## Hash sharding

In hash sharding, the data is randomly and evenly distributed across the tablets. Each tablet owns a range of hash values in the 2-byte hash space - `0x0000 - 0xFFFF`. For instance, in a 3-tablet table, the hash space could be split as `0x0000 - 0x5554`, `0x5555 - 0xAAA9`, `0xAAAA - 0xFFFF` with each tablet being responsible for one of the ranges.

In a hash-sharded table, even though the data is distributed well across all available tablets, within each tablet, the data is stored in the sorted order of the hash of the primary key. This leads to fast lookups but lower performance of range queries like greater than and lesser than (e.g., `age` < 20` ).

### Behavior

Let's understand the behavior of hash sharding via an example. Consider a simple table of users for which the user-id is the primary key.

```sql
CREATE TABLE user_hash (
    id int,
    name VARCHAR,
    PRIMARY KEY(id HASH)
);
```

{{<note>}}
For clarity, `HASH` has been explicitly added to the primary key definition to specify that this is a hash-sharded table. Hash sharding is the default distribution scheme. You can change the default using the `yb_use_hash_splitting_by_default` flag.
{{</note>}}

Now, let's add 6 rows to the table.

```sql
INSERT INTO user_hash VALUES (1, 'John Wick'), (2, 'Harry Potter'), (3, 'Bruce Lee'),
                             (4, 'India Jones'), (5, 'Vito Corleone'), (6, 'Jack Sparrow');
```

Now fetch all the rows from the table.

```sql
SELECT * FROM user_hash ;
```

You will see an output like :

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

Note that there is no ordering of the primary key `id`. This is because the rows are internally ordered on the hash of the id, which can be figured out using the function, `yb_hash_code(id)`.

As we have set up a cluster with 3 nodes, 3 tablets would have been created by default. The reason why the table can be split into multiple tablets easily in hash sharding is that the hash range is fixed `[0x0000 - 0xFFFF]`, exactly 65536 numbers and hence it is easy to divide this range across different tablets. So the inserted rows would have been distributed across the 3 tablets.   Let's verify this.

Now if you run a execute the same command with EXPLAIN ANALYZE as,

```sql
EXPLAIN (ANALYZE, DIST, COSTS OFF) SELECT * FROM user_hash ;
```

You will see an output like:

```puppet{.nocopy}
                           QUERY PLAN
-----------------------------------------------------------------
 Seq Scan on user_hash (actual time=0.526..2.054 rows=6 loops=1)
   Storage Table Read Requests: 3
   Storage Table Read Execution Time: 1.943 ms
   Storage Table Rows Scanned: 6
 Planning Time: 0.042 ms
 Execution Time: 2.085 ms
 Storage Read Requests: 3
 Storage Read Execution Time: 1.943 ms
 Storage Rows Scanned: 6
 Storage Write Requests: 0
 Catalog Read Requests: 0
 Catalog Write Requests: 0
 Storage Flush Requests: 0
 Storage Execution Time: 1.943 ms
 Peak Memory Usage: 24 kB
```

The `Storage Rows Scanned: 6` metric clearly says that 6 rows were read and `rows=6` on the first line says that 6 rows were returned. Now pay attention to the `Storage Table Read Requests: 3`. This says that there were 3 different read requests to the storage subsystem. This is because 3 different requests have been sent to the 3 different tablets to retrieve the rows from each of them.

{{<tip title="Remember">}}
Hash sharding is good for point lookups. In a hash-sharded table, data is evenly distributed and is ordered on the ordering of the hash of the primary key.
{{</tip>}}

## Range sharding

In range sharding, there are no hashes involved and the data is split into contiguous ranges of the primary key (in the sort order as defined in the table). Typically tables start as a single tablet and as the data grows and reaches a size threshold, the tablet dynamically splits into two. Tables can also be pre-split during creation.

The primary advantage of range sharding is that it is fast both for range scans and point lookups. The downside to this sharding scheme is that it starts with only one tablet even though there could be multiple nodes in the cluster. The other concern is that if the data being inserted is already coming in sorted, all new inserts would go to only one tablet, even though there are multiple tablets in the cluster.

### Behavior

Let's understand the behavior of range sharding via an example. Consider a simple table of users for which the user-id is the primary key.

```sql
CREATE TABLE user_range (
    id int,
    name VARCHAR,
    PRIMARY KEY(id ASC)
);
```

{{<note>}}
The word **ASC** has been explicitly added to the primary key definition to specify that this is a range-sharded table. This defines the ordering of the rows in the tablet. You can specify **DESC** if you want the rows to be stored in the descending order of the user id.
{{</note>}}

Now, let's add 6 rows to the table.

```sql
INSERT INTO user_range VALUES (1, 'John Wick'), (2, 'Harry Potter'), (3, 'Bruce Lee'),
                              (4, 'India Jones'), (5, 'Vito Corleone'), (6, 'Jack Sparrow');
```

Now fetch all the rows from the table.

```sql
SELECT * FROM user_range ;
```

If you select all the rows from the table, you will see an output like:

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

Note that all the rows are ordered in the ascending order of the `id`. This is because we have specified `id ASC` in the primary key definition.

Even though we have set up a cluster with 3 nodes, only 1 tablet would have been created by default. The reason why a range-sharded table cannot be split into multiple tablets automatically as the range of numbers is not fixed (actually infinite) and it is inefficient for the system to decide which range of numbers should be stored in which tablet. So the inserted rows will be stored only on this tablet. Let's verify this.

Now if you run a execute the same command with EXPLAIN ANALYZE as,

```sql
EXPLAIN (ANALYZE, DIST, COSTS OFF) SELECT * FROM user_range ;
```

You will see an output like:

```puppet{.nocopy}
                            QUERY PLAN
------------------------------------------------------------------
 Seq Scan on user_range (actual time=0.865..0.867 rows=6 loops=1)
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 0.729 ms
   Storage Table Rows Scanned: 6
 Planning Time: 0.050 ms
 Execution Time: 0.915 ms
 Storage Read Requests: 1
 Storage Read Execution Time: 0.729 ms
 Storage Rows Scanned: 6
 Storage Write Requests: 0
 Catalog Read Requests: 0
 Catalog Write Requests: 0
 Storage Flush Requests: 0
 Storage Execution Time: 0.729 ms
 Peak Memory Usage: 24 kB
 ```

The `Storage Rows Scanned: 6` metric clearly says that 6 rows were read and `rows=6` on the first line says that 6 rows were returned.

Now pay attention to the **`Storage Table Read Requests: 1`**. This says that there was just 1  read request to the storage subsystem. This is because there is only one tablet and all the rows are stored in it.

{{<tip title="Remember">}}
Range sharding is good for both range queries and point lookups. In a range sharded table, data is ordered on the ordering of the primary key.
{{</tip>}}

## Learn more

- [The internals of sharding](../../../architecture/docdb-sharding/sharding)
- [Data distribution](../../linear-scalability/data-distribution)