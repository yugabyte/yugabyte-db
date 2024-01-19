---
title: Table join methodologies in YSQL
headerTitle: Join strategies in YSQL
linkTitle: Join Strategies
description: Understand the various methodologies used for joining multiple tables
headcontent: Understand the various methods used for joining multiple tables
menu:
  preview:
    name: Join strategies
    identifier: explore-joins-ysql
    parent: explore
    weight: 270
type: docs
---

[Joins](../../ysql-language-features/queries#join-columns) are a fundamental concept in relational databases for querying and combining data from multiple tables. They are the mechanism used to combine rows from two or more tables based on a related column between them. The related column is usually a foreign key that establishes a relationship between the tables.

Although as a user you would write your query using one of the standard joins - [Inner](../../ysql-language-features/queries/#inner-join), [Left outer](../../ysql-language-features/queries/#left-outer-join), [Right outer](../../ysql-language-features/queries/#right-outer-join), [Full outer](../../ysql-language-features/queries/#full-outer-join), [Cross](../../ysql-language-features/queries/#cross-join)), the query planner will choose from one of many join strategies to execute the query and fetch results.  

The query optimizer is responsible for determining the most efficient join strategy for a given query, aiming to minimize the computational cost and improve overall performance. Knowing these strategies will help you understand the performance of your queries.

## Setup

For the purpose of illustration, we will pick this sample schema and dataset.

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
```

Lets load some data into the above tables. Add some students.

```sql
INSERT INTO students (id, name) 
VALUES (1,'Natasha'), (2,'Lisa'), (3,'Beverly'), (4,'Jeremy'), (5,'Mike'),
       (6,'Anthony'), (7,'Michael'), (8,'Caitlin'), (9,'Tracy'), (10,'Johnny'),
       (11,'Margaret'), (12,'Andrea'), (13,'Dylan'), (14,'Diana'), (15,'Nancy'),
       (16,'Toni'), (17,'Daniel'), (18,'Lori'), (19,'Jonathan'), (20,'Samantha');
```

Add some scores to the students.

```sql
INSERT INTO scores ( id,subject,score )
VALUES (1,'English',70), (1,'History',60), (1,'Math',100), (1,'Science',40), (1,'Spanish',80), (2,'English',50), (2,'History',40), (2,'Math',90), (2,'Science',100), (2,'Spanish',40), (3,'English',100), (3,'History',80), (3,'Math',100), (3,'Science',60), (3,'Spanish',50), (4,'English',100), (4,'History',40), (4,'Math',40), (4,'Science',60), (4,'Spanish',70), (5,'English',90), (5,'History',100), (5,'Math',80), (5,'Science',60), (5,'Spanish',80), 
       (6,'English',60), (6,'History',90), (6,'Math',70), (6,'Science',90), (6,'Spanish',50), (7,'English',50), (7,'History',60), (7,'Math',70), (7,'Science',100), (7,'Spanish',80), (8,'English',70), (8,'History',90), (8,'Math',80), (8,'Science',40), (8,'Spanish',80), (9,'English',80), (9,'History',100), (9,'Math',90), (9,'Science',100), (9,'Spanish',50), (10,'English',80), (10,'History',80), (10,'Math',50), (10,'Science',60), (10,'Spanish',70), (11,'English',60), (11,'History',40),
       (11,'Math',50), (11,'Science',60), (11,'Spanish',50), (12,'English',60), (12,'History',70), (12,'Math',70), (12,'Science',70), (12,'Spanish',80), (13,'English',100), (13,'History',90), (13,'Math',50), (13,'Science',100), (13,'Spanish',60), (14,'English',100), (14,'History',70), (14,'Math',90), (14,'Science',90), (14,'Spanish',90), (15,'English',100), (15,'History',40), (15,'Math',50), (15,'Science',80), (15,'Spanish',70),
       (16,'English',60), (16,'History',80), (16,'Math',90), (16,'Science',50), (16,'Spanish',70), (17,'English',100), (17,'History',70), (17,'Math',80), (17,'Science',50), (17,'Spanish',60), (18,'English',50), (18,'History',100), (18,'Math',80), (18,'Science',70), (18,'Spanish',100), (19,'English',70), (19,'History',80), (19,'Math',90), (19,'Science',60), (19,'Spanish',90), (20,'English',50), (20,'History',80), (20,'Math',70), (20,'Science',40), (20,'Spanish',80);
```

Run the following command to ensure that the tables are analyzed for the optimizer.

```sql
analyze students, scores;
```

{{<tip>}}
In the following examples, we will use **hints** to force the query planner to pick a specific join strategy. In practice, you do **not** need to specify a hint, the optimizer will pick the right strategy.
{{</tip>}}

## Nested Loop

Nested Loop Join is the simplest join algorithm. It involves iterating through each row of the first table and checking for matches in the second table based on the join condition. It has an outer loop and an inner loop. The outer loop iterates through the rows of the first (outer) table, while the inner loop iterates through the rows of the second (inner) table. The worst case time complexity is `O(n^2)`. Often used when one table is small and an index can be utilized. This is also the preferred join strategy at times as it is the only join method to not require extra memory overhead and also operate well in queries where the join clause has low selectivity.

To fetch all students who have scored more than `70` in any subject using Nested loop join, you would execute,

```sql
explain (analyze, dist, costs off)
/*+
    nestloop(students scores)
    set(yb_enable_optimizer_statistics on)
*/
SELECT name, subject, score FROM students JOIN scores USING(id) WHERE score > 70;
```

The query plan would be similar to:

```sql
 Nested Loop (actual time=2002.047..2002.491 rows=47 loops=1)
   Join Filter: (students.id = scores.id)
   Rows Removed by Join Filter: 473
   ->  Seq Scan on scores (actual time=1001.335..1001.359 rows=47 loops=1)
         Remote Filter: (score > 70)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1001.212 ms
   ->  Materialize (actual time=21.292..21.294 rows=11 loops=47)
         ->  Seq Scan on students (actual time=1000.681..1000.696 rows=20 loops=1)
               Storage Table Read Requests: 1
               Storage Table Read Execution Time: 1000.591 ms
 Planning Time: 0.192 ms
 Execution Time: 2002.565 ms
 Storage Read Requests: 2
 Storage Read Execution Time: 2001.802 ms
 Storage Execution Time: 2001.802 ms
 Peak Memory Usage: 92 kB
```

## Merge Join

In a Merge Join, both input tables must be sorted on the join key. It works by merging the sorted input streams based on the join condition. Merge Joins are efficient when dealing with large datasets, and the join condition involves equality comparisons. If the tables are not already sorted, the optimizer will use sorting operations as part of the execution plan.

As the join key columns are now sorted, the executer can compare the 2 streams easily in `O(n+m)` time complexity. Merge joins are typically suitable when joining based on equality and are commonly used when the join keys are indexed or the data is naturally sorted.

To fetch all students who have scored more than `70` in any subject and use Merge join, you would execute,

```sql
explain (analyze, dist, costs off)
/*+
    mergejoin(students scores)
    set(yb_enable_optimizer_statistics on)
*/
SELECT name, subject, score FROM students JOIN scores USING(id) WHERE score > 70;
```

The query plan would be similar to:

```sql
 Merge Join (actual time=2001.954..2002.009 rows=47 loops=1)
   Merge Cond: (students.id = scores.id)
   ->  Sort (actual time=1000.740..1000.745 rows=20 loops=1)
         Sort Key: students.id
         Sort Method: quicksort  Memory: 25kB
         ->  Seq Scan on students (actual time=1000.707..1000.717 rows=20 loops=1)
               Storage Table Read Requests: 1
               Storage Table Read Execution Time: 1000.611 ms
   ->  Sort (actual time=1001.205..1001.213 rows=47 loops=1)
         Sort Key: scores.id
         Sort Method: quicksort  Memory: 27kB
         ->  Seq Scan on scores (actual time=1001.133..1001.152 rows=47 loops=1)
               Remote Filter: (score > 70)
               Storage Table Read Requests: 1
               Storage Table Read Execution Time: 1001.020 ms
 Planning Time: 0.203 ms
 Execution Time: 2002.086 ms
 Storage Read Requests: 2
 Storage Read Execution Time: 2001.631 ms
 Storage Execution Time: 2001.631 ms
 Peak Memory Usage: 118 kB
```

## Hash Join

The Hash Join algorithm starts by hashing the join key columns of both input tables. The hash function transforms the join key values into a hash code. Hash Joins are suitable for equi-joins (joins based on equality) and are effective when the join keys are not sorted or indexed.

To fetch all students who have scored more than `70` in any subject and use Hash join, you would execute,

```sql
explain (analyze, dist, costs off)
/*+
    hashjoin(students scores)
    set(yb_enable_optimizer_statistics on)
*/
SELECT name, subject, score FROM students JOIN scores USING(id) WHERE score > 70;
```

The query plan would be similar to:

```sql
 Hash Join (actual time=2002.455..2002.516 rows=47 loops=1)
   Hash Cond: (scores.id = students.id)
   ->  Seq Scan on scores (actual time=1001.456..1001.484 rows=47 loops=1)
         Remote Filter: (score > 70)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1001.324 ms
   ->  Hash (actual time=1000.968..1000.969 rows=20 loops=1)
         Buckets: 1024  Batches: 1  Memory Usage: 9kB
         ->  Seq Scan on students (actual time=1000.906..1000.919 rows=20 loops=1)
               Storage Table Read Requests: 1
               Storage Table Read Execution Time: 1000.814 ms
 Planning Time: 0.280 ms
 Execution Time: 2002.591 ms
 Storage Read Requests: 2
 Storage Read Execution Time: 2002.138 ms
 Storage Execution Time: 2002.138 ms
 Peak Memory Usage: 133 kB
```

## Batched Nested Loop (BNL)

In the case of the Nested loop joins, the inner table is accessed multiple times one for each outer table row. This would lead to multiple RPC requests across the different nodes in the cluster, making this join strategy very slow as the outer table gets larger.

To reduce the number of requests sent across the nodes during the Nested Loop join, YugabyteDB adds new optimization to batch multiple keys of the outer table into one RPC request. This batch size can be controlled using the GUC variable `yb_bnl_batch_size`, which defaults to `1` (which effectively means that the feature is `OFF`). Suggested value for this variable is `1024`.

The optimizer will automatically adopt the batch size when other join strategies are not fit for the current query.

Now to fetch all students who have scored more than `70` in any subject using Batched nested loop join, you would execute,

```sql
explain (analyze, dist, costs off)
/*+
    ybbatchednl(students scores)
    set(yb_enable_optimizer_statistics on)
*/
SELECT name, subject, score FROM students JOIN scores USING(id) WHERE score > 70;
```

And the query plan would be similar to:

```sql
 YB Batched Nested Loop Join (actual time=2002.102..2002.181 rows=47 loops=1)
   Join Filter: (students.id = scores.id)
   ->  Seq Scan on students (actual time=1000.742..1000.753 rows=20 loops=1)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1000.645 ms
   ->  Index Scan using scores_pkey on scores (actual time=1001.317..1001.346 rows=47 loops=1)
         Index Cond: (id = ANY (ARRAY[students.id, $1, $2, ..., $19]))
         Remote Filter: (score > 70)
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 1001.139 ms
 Planning Time: 0.204 ms
 Execution Time: 2002.266 ms
 Storage Read Requests: 2
 Storage Read Execution Time: 2001.784 ms
 Storage Execution Time: 2001.784 ms
 Peak Memory Usage: 56 kB
```

## Learn more

- [Sql Joins](../../ysql-language-features/queries#join-columns)
- [Postgres Join Methods in YugabyteDB](https://dev.to/yugabyte/postgresql-join-methods-in-yugabytedb-f02)
- [Batched Nested Loop to reduce read requests to the distributed storage](https://dev.to/yugabyte/batched-nested-loop-to-reduce-read-requests-to-the-distributed-storage-j5i)
- [**OR** Filter on Two Tables](https://dev.to/yugabyte/or-filter-on-two-tables-and-batched-nested-loops-aa)