---
title: Table join methodologies in YSQL
headerTitle: Join strategies in YSQL
linkTitle: Join Strategies
description: Understand the various methodologies used for joining multiple tables
headcontent: Understand the various methods used for joining multiple tables
menu:
  stable:
    name: Join strategies
    identifier: explore-joins-ysql
    parent: explore-ysql-language-features
    weight: 214
type: docs
---

[Joins](../../ysql-language-features/queries#join-columns) are a fundamental concept in relational databases for querying and combining data from multiple tables. They are the mechanism used to combine rows from two or more tables based on a related column between them. The related column is usually a foreign key that establishes a relationship between the tables. A join condition specifies how the rows from one table should be matched with the rows from another table. It can be defined in one of `ON`, `USING`, or `WHERE` clauses.

Although as a user you would write your query using one of the standard joins - [Inner](../queries/#inner-join), [Left outer](../queries/#left-outer-join), [Right outer](../queries/#right-outer-join), [Full outer](../queries/#full-outer-join), or [Cross](../queries/#cross-join), the query planner will choose from one of many join strategies to execute the query and fetch results.

The query optimizer is responsible for determining the most efficient join strategy for a given query, aiming to minimize the computational cost and improve overall performance. Knowing these strategies will help you understand the performance of your queries.

{{% explore-setup-single %}}

## Setup

Suppose you work with a database that includes the following tables and indexes:

```sql
CREATE TABLE students (
    id int,
    name varchar(255),
    PRIMARY KEY(id)
);

CREATE TABLE scores (
    id int,
    subject varchar(100),
    score int,
    PRIMARY KEY(id, subject)
);

CREATE INDEX idx_name on students(name);
CREATE INDEX idx_id on scores(id);
```

Load some data into the tables by adding some students:

```sql
INSERT INTO students (id,name) 
   SELECT n, (ARRAY['Natasha', 'Lisa', 'Mike', 'Michael', 'Anthony'])[floor(random() * 4 + 1)] 
      FROM generate_series(1, 20) AS n;
```

Add some scores to the students as follows:

```sql
WITH subjects AS (
    SELECT unnest(ARRAY['English', 'History', 'Math', 'Spanish', 'Science']) AS subject
)
INSERT INTO scores (id, subject, score)
    SELECT id, subject, (40+random()*60)::int AS score
        FROM subjects CROSS JOIN students ORDER BY id;
```

Run the following command to ensure that the tables are analyzed for the optimizer:

```sql
analyze students, scores;
```

{{<tip>}}
The following examples use _hints_ to force the query planner to pick a specific join strategy. In practice, you don't need to specify a hint, as the default planner will pick an appropriate strategy.
{{</tip>}}

## Nested loop join

Nested loop join is the simplest join algorithm. It involves iterating through each row of the first table and checking for matches in the second table based on the join condition.

Nested loop join has an outer loop and an inner loop. The outer loop iterates through the rows of the first (outer) table, while the inner loop iterates through the rows of the second (inner) table.

The worst-case time complexity is `O(m*n)`, where `m` and `n` are the sizes of outer and inner tables respectively. Often used when one table is small and an index can be used.

This is also the preferred join strategy at times as it is the only join method to not require extra memory overhead and also operates well in queries where the join clause has low selectivity. If you compare the `Peak Memory Usage` of this join strategy with others, you will notice that this is the lowest.

To fetch all scores of students named `Natasha` who have scored more than `70` in any subject using Nested loop join, you would execute the following:

```sql
explain (analyze, dist, costs off)
/*+
    set(enable_material false)
    nestloop(students scores)
    set(yb_enable_optimizer_statistics on)
*/
SELECT name, subject, score 
      FROM students JOIN scores USING(id) 
      WHERE name = 'Natasha' and score > 70;
```

The query plan would be similar to the following:

```yaml
 Nested Loop (actual time=2002.483..9011.038 rows=18 loops=1)
   ->  Seq Scan on students (actual time=1001.369..1001.393 rows=8 loops=1)
         Remote Filter: ((name)::text = 'Natasha'::text)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1001.245 ms
   ->  Index Scan using scores_pkey on scores (actual time=1001.141..1001.146 rows=2 loops=8)
         Index Cond: (id = students.id)
         Remote Filter: (score > 70)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1001.050 ms
 Planning Time: 0.308 ms
 Execution Time: 9011.109 ms
 Storage Read Requests: 9
 Storage Read Execution Time: 9009.641 ms
 Storage Execution Time: 9009.641 ms
 Peak Memory Usage: 56 kB
```

## Merge join

In a Merge join, both input tables must be sorted on the join key. It works by merging the sorted input streams based on the join condition.

Merge Joins are efficient when dealing with large datasets, and the join condition involves equality comparisons. If the tables are not already sorted, the optimizer will use sorting operations as part of the execution plan.

As the join key columns are now sorted, the executor can compare the two streams easily in `O(n+m)` time complexity. Merge joins are typically suitable when joining based on equality and are commonly used when the join keys are indexed or the data is naturally sorted.

To fetch all scores of students named `Natasha` who have scored more than `70` in any subject and use Merge join, you would execute the following:

```sql
explain (analyze, dist, costs off)
/*+
    mergejoin(students scores)
    set(yb_enable_optimizer_statistics on)
*/
SELECT name, subject, score 
      FROM students JOIN scores USING(id) 
      WHERE name = 'Natasha' and score > 70;
```

The query plan would be similar to the following:

```yaml
 Merge Join (actual time=2002.906..2002.947 rows=18 loops=1)
   Merge Cond: (students.id = scores.id)
   ->  Sort (actual time=1001.443..1001.445 rows=8 loops=1)
         Sort Key: students.id
         Sort Method: quicksort  Memory: 25kB
         ->  Seq Scan on students (actual time=1001.418..1001.424 rows=8 loops=1)
               Remote Filter: ((name)::text = 'Natasha'::text)
               Storage Table Read Requests: 1
               Storage Table Read Execution Time: 1001.243 ms
   ->  Sort (actual time=1001.447..1001.455 rows=51 loops=1)
         Sort Key: scores.id
         Sort Method: quicksort  Memory: 27kB
         ->  Seq Scan on scores (actual time=1001.370..1001.390 rows=51 loops=1)
               Remote Filter: (score > 70)
               Storage Table Read Requests: 1
               Storage Table Read Execution Time: 1001.250 ms
 Planning Time: 0.252 ms
 Execution Time: 2003.039 ms
 Storage Read Requests: 2
 Storage Read Execution Time: 2002.493 ms
 Storage Execution Time: 2002.493 ms
 Peak Memory Usage: 126 kB
```

## Hash join

The Hash join algorithm starts by hashing the join key columns of both input tables. The hash function transforms the join key values into a hash code.

Hash Joins are suitable for equi-joins (joins based on equality) and are effective when the join keys are not sorted or indexed.

To fetch all scores of students named `Natasha` who have scored more than `70` in any subject using Hash join, you would execute the following:

```sql
explain (analyze, dist, costs off)
/*+
    hashjoin(students scores)
    set(yb_enable_optimizer_statistics on)
*/
SELECT name, subject, score 
      FROM students JOIN scores USING(id) 
      WHERE name = 'Natasha' and score > 70;
```

The query plan would be similar to the following:

```yaml
 Hash Join (actual time=2002.431..2002.477 rows=18 loops=1)
   Hash Cond: (scores.id = students.id)
   ->  Seq Scan on scores (actual time=1001.043..1001.061 rows=51 loops=1)
         Remote Filter: (score > 70)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1000.927 ms
   ->  Hash (actual time=1001.360..1001.361 rows=8 loops=1)
         Buckets: 1024  Batches: 1  Memory Usage: 9kB
         ->  Seq Scan on students (actual time=1001.327..1001.335 rows=8 loops=1)
               Remote Filter: ((name)::text = 'Natasha'::text)
               Storage Table Read Requests: 1
               Storage Table Read Execution Time: 1001.211 ms
 Planning Time: 0.231 ms
 Execution Time: 2002.540 ms
 Storage Read Requests: 2
 Storage Read Execution Time: 2002.137 ms
 Storage Execution Time: 2002.137 ms
 Peak Memory Usage: 141 kB
```

## Batched nested loop join (BNL)

In the case of Nested loop joins, the inner table is accessed multiple times, once for each outer table row. This leads to multiple RPC requests across the different nodes in the cluster, making this join strategy very slow as the outer table gets larger.

To reduce the number of requests sent across the nodes during the Nested loop join, YugabyteDB adds an optimization to batch multiple keys of the outer table into one RPC request. This batch size can be controlled using the YSQL configuration parameter `yb_bnl_batch_size`, which defaults to `1` (which effectively means that the feature is `OFF`). The suggested value for this variable is `1024`.

If `yb_bnl_batch_size` is greater than `1`, the optimizer will try to adopt the batching optimization when other join strategies are not fit for the current query.

To fetch all scores of students named `Natasha` who have scored more than `70` in any subject using Batched nested loop join, you would execute the following:

<!--
{{/* TOD0: fix this after 2.21 */}}
-->

```sql
-- For versions <  2.21 : nestloop(students scores)
-- For versions >= 2.21 : ybbatchednl(students scores)
explain (analyze, dist, costs off)
/*+
    nestloop(students scores)
    set(yb_bnl_batch_size 1024)
    set(yb_enable_optimizer_statistics on)
*/
SELECT name, subject, score 
      FROM students JOIN scores USING(id) 
      WHERE name = 'Natasha' and score > 70;
```

The query plan would be similar to the following:

```yaml
 YB Batched Nested Loop Join (actual time=4004.255..4004.297 rows=18 loops=1)
   Join Filter: (students.id = scores.id)
   ->  Index Scan using idx_name on students (actual time=2001.861..2001.870 rows=8 loops=1)
         Index Cond: ((name)::text = 'Natasha'::text)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1001.078 ms
         Storage Index Read Requests: 1
         Storage Index Read Execution Time: 1000.589 ms
   ->  Index Scan using idx_id on scores (actual time=2002.290..2002.305 rows=18 loops=1)
         Index Cond: (id = ANY (ARRAY[students.id, $1, $2, ..., $1023]))
         Remote Filter: (score > 70)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1001.356 ms
         Storage Index Read Requests: 1
         Storage Index Read Execution Time: 1000.670 ms
 Planning Time: 0.631 ms
 Execution Time: 4004.541 ms
 Storage Read Requests: 4
 Storage Read Execution Time: 4003.693 ms
 Storage Execution Time: 4003.693 ms
 Peak Memory Usage: 476 kB
```

The key point to note is the index condition:

```sql
(id = ANY (ARRAY[students.id, $1, $2, ..., $1023]))
```

which led to `loops=1` in the inner scan as the lookup was batched. In the case of [nested loop join](#nested-loop-join), the inner side looped 8 times. Such batching can drastically improve performance in many scenarios.

## Learn more

- [SQL Joins](../../ysql-language-features/queries#join-columns)
- [Postgres Join Methods in YugabyteDB](https://dev.to/yugabyte/postgresql-join-methods-in-yugabytedb-f02)
- [Batched Nested Loop to reduce read requests to the distributed storage](https://dev.to/yugabyte/batched-nested-loop-to-reduce-read-requests-to-the-distributed-storage-j5i)
- [OR Filter on Two Tables](https://dev.to/yugabyte/or-filter-on-two-tables-and-batched-nested-loops-aa)
- [Batched Nested Loop for Join With Large Pagination](https://dev.to/yugabyte/batched-nested-loop-for-join-with-pagination-621)
