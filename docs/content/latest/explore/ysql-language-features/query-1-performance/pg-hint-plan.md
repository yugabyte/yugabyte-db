---
title: Optimizing YSQL queries using pg_hint_plan
linkTitle: Optimizing YSQL queries using pg_hint_plan
description: Query optimization of YSQL queries using pg_hint_plan
headerTitle: Optimizing YSQL queries using pg_hint_plan
image: /images/section_icons/index/develop.png
menu:
  latest:
    identifier: pg_hint_plan
    parent: query-1-performance
    weight: 566
isTocNested: true
showAsideToc: true
---

## Synopsis

Yugabyte (YB) leverages PostgreSQL’s pg_hint_plan Extension to control query execution plans with hinting phrases using comments.

Yugabyte uses PostgreSQL’s cost-based optimizer, which estimates the costs of each possible execution plan for an SQL statement. The execution plan with the lowest cost finally is executed. The planner does its best to select the best execution plan, but not perfect. Additionally, the version of YB's planner is sub-optimal. For instance, the cost-based optimizer is naive and assumes row counts for all tables to be 1000. Row counts however play a crucial role in calculating the cost estimates. To overcome these limitations, we enable pg_hint_plan.

pg_hint_plan makes it possible to tweak execution plans using "hints", which are simple descriptions in the form of SQL comments.


## Instructions

### Configuring pg_hint_plan

pg_hint plan comes pre-configured and enabled with Yugabyte.  GUC (Grand Unified Configuration) parameters below affect the behavior of pg_hint_plan is as follows:-


<table>
  <tr>
   <td><strong>Option</strong>
   </td>
   <td><strong>Values notes</strong>
   </td>
  </tr>
  <tr>
   <td><code>pg_hint_plan.enable_hint</code>
   </td>
   <td>Enables or disables the function of pg_hint_plan.
<p>
The default is <strong>ON</strong>.
   </td>
  </tr>
  <tr>
   <td><code>pg_hint_plan.debug_print</code>
   </td>
   <td>Enable and select the verbosity of the debug output of pg_hint_plan. off, on, detailed, and verbose are valid configuration options.
<p>
The default is <strong>OFF</strong>.
   </td>
  </tr>
  <tr>
   <td><code>pg_hint_plan.message_level</code>
   </td>
   <td>Specifies the message level of debug prints. error, warning, notice, info, log, debug are valid message levels while fatal and panic are inhibited.
<p>
The default is <strong>INFO</strong>.
   </td>
  </tr>
</table>


### Enabling pg_hint_plan

`pg_hint_plan.enable_hint `is a GUC parameter that controls enabling pg_hint_plan.

To enable pg_hint_plan, execute


```sql
SET pg_hint_plan.enable_hint=ON;
```


The default is **ON**.

## pg_hint_plan features with examples

### Initialization

We use the following table and index definitions to illustrate the features present in pg_hint_plan.


```sql
CREATE TABLE t1 (id int PRIMARY KEY, val int);
CREATE TABLE t2 (id int PRIMARY KEY, val int);
CREATE TABLE t3 (id int PRIMARY KEY, val int);

INSERT INTO t1 SELECT i, i % 100 FROM (SELECT generate_series(1, 10000) i) t;
INSERT INTO t2 SELECT i, i % 10 FROM (SELECT generate_series(1, 1000) i) t;
INSERT INTO t3 SELECT i, i FROM (SELECT generate_series(1, 100) i) t;

CREATE INDEX t1_val ON t1 (val);
CREATE INDEX t2_val ON t2 (val);
CREATE INDEX t3_id1 ON t3 (id);
CREATE INDEX t3_id2 ON t3 (id);
CREATE INDEX t3_id3 ON t3 (id);
CREATE INDEX t3_val ON t3 (val);
```


The schema of the following tables after executing the above commands is as follows.


```sql
                 Table "public.t1"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 id     | integer |           | not null |
 val    | integer |           |          |
Indexes:
    "t1_pkey" PRIMARY KEY, lsm (id HASH)
    "t1_val" lsm (val HASH)

                 Table "public.t2"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 id     | integer |           | not null |
 val    | integer |           |          |
Indexes:
    "t2_pkey" PRIMARY KEY, lsm (id HASH)
    "t2_val" lsm (val HASH)

                 Table "public.t3"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 id     | integer |           | not null |
 val    | integer |           |          |
Indexes:
    "t3_pkey" PRIMARY KEY, lsm (id HASH)
    "t3_id1" lsm (id HASH)
    "t3_id2" lsm (id HASH)
    "t3_id3" lsm (id HASH)
    "t3_val" lsm (val HASH)
```



### How to use pg_hint_plan?

pg_hint_plan parses hinting phrases of a special form present in SQL statements. This special form begins with the character sequence "/*+" and ends with "*/". Hint phrases consist of hint names followed by hint parameters enclosed within parentheses delimited by spaces.

In the example below, `HashJoin` is selected as the joining method for joining `pg_bench_branches` and `pg_bench_accounts` and a `SeqScan` is used for scanning the table `pgbench_accounts`.


```sql
yugabyte=# /*+
yugabyte*#    HashJoin(a b)
yugabyte*#    SeqScan(a)
yugabyte*#  */
yugabyte-# EXPLAIN SELECT *
yugabyte-#    FROM pgbench_branches b
yugabyte-#    JOIN pgbench_accounts a ON b.bid = a.bid
yugabyte-#   ORDER BY a.aid;
                                      QUERY PLAN
---------------------------------------------------------------------------------------
 Sort  (cost=31465.84..31715.84 rows=100000 width=197)
   Sort Key: a.aid
   ->  Hash Join  (cost=1.02..4016.02 rows=100000 width=197)
         Hash Cond: (a.bid = b.bid)
         ->  Seq Scan on pgbench_accounts a  (cost=0.00..2640.00 rows=100000 width=97)
         ->  Hash  (cost=1.01..1.01 rows=1 width=100)
               ->  Seq Scan on pgbench_branches b  (cost=0.00..1.01 rows=1 width=100)
(7 rows)
```

### Enabling debug prints for pg_hint_plan?

It is very useful to know the specific hints that pg_hint_plan utilizes and forwards to the query planner. There could be situations where syntactical errors or wrong hint names could be present in users’ hint phrases. Hence, to view these debug prints, users can execute the following commands.


```sql
yugabyte=# SET pg_hint_plan.debug_print TO on;
yugabyte=# \set SHOW_CONTEXT always
yugabyte=# SET client_min_messages TO log;
```



### Feature 1: Hints for scan methods

Scan method hints enforce the scanning method on tables when specified along with appropriate hint phrases. Users should specify their required scan method and their respective target tables by alias names if any. The list of scan methods that are supported by Yugabyte along with hint phrases from pg_hint_plan are as follows:

| Option                              |       Value                     |
| ----------------------------------- | ------------------------------- |
| SeqScan(table)                      |	    Enable SeqScan on the table.|
| NoSeqScan(table)	                  |  Do not enable SeqScan on the table. |
| IndexScan(table)	                  |  Enable IndexScan on the table. |
| IndexScan(table idx)	              |  Enable IndexScan on the table using the index idx. |
| NoIndexScan(table)	                |  Do not enable IndexScan on the table. |
| IndexOnlyScan(table)	              |  Enable IndexOnlyScan on the table. |
| NoIndexOnlyScan(table)	            |  Do not enable IndexOnlyScan on the table. |
| IndexScanRegexp(table regex)	      |  Enable index scan on the table whose indices match with the regular expression defined by regex. |
| IndexOnlyScanRegexp(table regex)	  |  Do not enable index scan on the table whose indices match with the regular expression defined by regex. |



``` sql
yugabyte=# /*+SeqScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
LOG:  pg_hint_plan:
used hint:
SeqScan(t2)
not used hint:
duplication hint:
error hint:

              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Seq Scan on t2
   ->  Index Scan using t1_pkey on t1
         Index Cond: (id = t2.id)
(4 rows)
```

``` sql
yugabyte=# /*+SeqScan(t1)IndexScan(t2)*/
yugabyte-# EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
LOG:  pg_hint_plan:
used hint:
SeqScan(t1)
IndexScan(t2)
not used hint:
duplication hint:
error hint:

              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Seq Scan on t1
   ->  Index Scan using t2_pkey on t2
         Index Cond: (id = t1.id)
(4 rows)
```

``` sql
yugabyte=# /*+NoIndexScan(t1)*/
yugabyte-# EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
LOG:  pg_hint_plan:
used hint:
NoIndexScan(t1)
not used hint:
duplication hint:
error hint:

              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Seq Scan on t1
   ->  Index Scan using t2_pkey on t2
         Index Cond: (id = t1.id)
(4 rows)
```


In the first example, the hint `/*+SeqScan(t2)*/` allows table `t2` to be scanned using `SeqScan`. However, in the second example, due to the hint `/*+SeqScan(t1)IndexScan(t2)*/`, `t2` is scanned using IndexScan, and `t1` is scanned using `SeqScan`. Users can also use hint phrases to instruct the query planner not to use a specific type of scan. As shown in example 3, the hint `/*+NoIndexScan(t1)*/` can restrict `IndexScan` on table `t1`.


#### Specifying indices in hint phrases

A single table can have many indices. Using pg_hint_plan, users can specify the exact index to use while performing scans on a table. Consider table `t3` as an example. From its description the earlier section, we can see that it contains multiple indices for the column `id` (`t3_pkey, t3_id1, t3_id2, t3_id3`). Users can choose any of these indices to run scans by executing the SQL queries with the appropriate hint phrases.


``` sql
yugabyte=# EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
           QUERY PLAN
--------------------------------
 Index Scan using t3_pkey on t3
   Index Cond: (id = 1)
(2 rows)
```

``` sql
yugabyte=# /*+IndexScan(t3 t3_id2)*/
yugabyte-# EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
LOG:  available indexes for IndexScan(t3): t3_id2
LOG:  pg_hint_plan:
used hint:
IndexScan(t3 t3_id2)
not used hint:
duplication hint:
error hint:

          QUERY PLAN
-------------------------------
 Index Scan using t3_id2 on t3
   Index Cond: (id = 1)
(2 rows)
```

``` sql
yugabyte=# /*+IndexScan(t3 no_exist)*/
yugabyte-# EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
LOG:  available indexes for IndexScan(t3):
LOG:  pg_hint_plan:
used hint:
IndexScan(t3 no_exist)
not used hint:
duplication hint:
error hint:

     QUERY PLAN
--------------------
 Seq Scan on t3
   Filter: (id = 1)
(2 rows)
```

``` sql
yugabyte=# /*+IndexScan(t3 t3_id1 t3_id2)*/
yugabyte-# EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
LOG:  available indexes for IndexScan(t3): t3_id2 t3_id1
LOG:  pg_hint_plan:
used hint:
IndexScan(t3 t3_id1 t3_id2)
not used hint:
duplication hint:
error hint:

          QUERY PLAN
-------------------------------
 Index Scan using t3_id2 on t3
   Index Cond: (id = 1)
(2 rows)

```


In the above example, the first query is executed without any hint phrase. Hence, the index scan uses the primary key index `t3_pkey`. However, the second query contains the hint `/*+IndexScan(t3 t3_id2)*/`, and hence it uses the secondary index `t3_id2` to perform the index scan. When the hint phrase `/*+IndexScan(t3 no_exist)*/` is provided, the planner reverts to `SeqScan` as none of the indices can be used.  Users can also provide a selective list of indices in the hint phrases. The query planner will choose among those indices in such circumstances.

### Feature 2: Hints for join methods

Join method hints enforces the join methods for SQL statements. Using pg_hint_plan, we can specify the methodology by which tables should be joined.

| Option                              |       Value                     |
| ----------------------------------- | ------------------------------- |
| HashJoin(t1 t2 t3 ...)	            |  Join t1, t2, and t3 using HashJoin. |
| NoHashJoin(t1 t2 t3 ...)	          |  Do not join t1, t2, and t3 using HashJoin.|
| NestLoop(t1 t2 t3 ...)	            |  Join t1, t2, and t3 using NestLoop join. |
| NoNestLoop(t1 t2 t3 ...)	          |  Do not join t1, t2, and t3 using NestLoop join.|

```sql
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
LOG:  pg_hint_plan:
used hint:
HashJoin(t1 t2)
not used hint:
duplication hint:
error hint:

          QUERY PLAN
------------------------------
 Hash Join
   Hash Cond: (t1.id = t2.id)
   ->  Seq Scan on t1
   ->  Hash
         ->  Seq Scan on t2
(5 rows)
```

```sql
/*+NestLoop(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
LOG:  pg_hint_plan:
used hint:
NestLoop(t1 t2)
not used hint:
duplication hint:
error hint:

              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Seq Scan on t1
   ->  Index Scan using t2_pkey on t2
         Index Cond: (id = t1.id)
(4 rows)
```


In this example, the first query uses a `HashJoin` on tables `t1` and `t2` respectively while the second query uses a `NestedLoop` join for the same. The required join methods are specified in their respective hint phrases. Users can utilize multiple hint phrases to combine join methods and scan methods.


```sql
yugabyte=# /*+NestLoop(t2 t3 t1) SeqScan(t3) SeqScan(t2)*/
yugabyte-# EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3
yugabyte-# WHERE t1.id = t2.id AND t1.id = t3.id;
LOG:  pg_hint_plan:
used hint:
SeqScan(t2)
SeqScan(t3)
NestLoop(t1 t2 t3)
not used hint:
duplication hint:
error hint:

              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Hash Join
         Hash Cond: (t2.id = t3.id)
         ->  Seq Scan on t2
         ->  Hash
               ->  Seq Scan on t3
   ->  Index Scan using t1_pkey on t1
         Index Cond: (id = t2.id)
(8 rows)


```


In the example given above, we use the hint `/*+NestLoop(t2 t3 t4 t1) SeqScan(t3) SeqScan(t4)*/ `enables NestLoop join on tables `t1, t2, t3, t4.` However, it enables `SeqScan` on tables `t3` and `t4` due to the hint phrases while the rest of the scans are performed using `IndexScan`.` `


### Feature 3: Hints for joining order

Joining order hints are used to execute joins in specific orders as enumerated in the parameter list of a hint phrase. Joining in a specific order can be enforced using the `Leading` hint.


```sql
yugabyte=# /*+Leading(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t1.id = t3.id;
                 QUERY PLAN
--------------------------------------------
 Nested Loop
   ->  Nested Loop
         ->  Seq Scan on t1
         ->  Index Scan using t2_pkey on t2
               Index Cond: (id = t1.id)
   ->  Index Scan using t3_pkey on t3
         Index Cond: (id = t1.id)
(7 rows)
```

```sql
yugabyte=# /*+Leading(t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t1.id = t3.id;
                 QUERY PLAN
--------------------------------------------
 Nested Loop
   ->  Nested Loop
         ->  Seq Scan on t2
         ->  Index Scan using t3_pkey on t3
               Index Cond: (id = t2.id)
   ->  Index Scan using t1_pkey on t1
         Index Cond: (id = t2.id)
(7 rows)

```


The joining order in the first query is `/*+Leading(t1 t2 t3)*/ `whereas the joining order for the second query is `/*+Leading(t2 t3 t1)*/`. You can see that the query plan’s order of execution following the joining order specified in the parameter lists of the hint phrases.


### Feature 4: Setting _work_mem_

Users can leverage the `work_mem` setting in PostgreSQL to improve the performance of slow queries that sort, join or aggregate large sets of table rows. A detailed description of its implication is illustrated in the following [link](https://andreigridnev.com/blog/2016-04-16-increase-work_mem-parameter-in-postgresql-to-make-expensive-queries-faster/). The following example shows how to enable `work_mem, `as a part of` pg_hint_plan.`


``` sql
yugabyte=# /*+Set(work_mem "1MB")*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
LOG:  pg_hint_plan:
used hint:
Set(work_mem 1MB)
not used hint:
duplication hint:
error hint:

              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Seq Scan on t1
   ->  Index Scan using t2_pkey on t2
         Index Cond: (id = t1.id)
(4 rows)
```



### Feature 5: GUC planner method configuration

Planner method configuration parameters provide a crude method of influencing the query plans chosen by the query optimizer. If the default plan chosen by the optimizer for a particular query is not optimal, a temporary solution is to use one of these configuration parameters to force the optimizer to choose a different plan. A more detailed explanation of the Planner Method Configuration can be found in the following [link](https://www.postgresql.org/docs/11/runtime-config-query.html). The list of configuration parameters supported by Yugabyte is as follows:

| Option  |
| ------- |
| enable_hashagg |
| enable_hashjoin |
| enable_indexscan |
| enable_indexonlyscan |
| enable_material |
| enable_nestloop |
| enable_partition_pruning |
| enable_partitionwise_join |
| enable_partitionwise_aggregate |
| enable_seqscan |
| enable_sort |


pg_hint_plan leverages the planner method configuration by embedding these configuration parameters in each query’s comment. Consider the following example:


```sql
yugabyte=# EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Seq Scan on t1
   ->  Index Scan using t2_pkey on t2
         Index Cond: (id = t1.id)
(4 rows)

yugabyte=# /*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
LOG:  pg_hint_plan:
used hint:
Set(enable_indexscan off)
not used hint:
duplication hint:
error hint:

          QUERY PLAN
------------------------------
 Hash Join
   Hash Cond: (t1.id = t2.id)
   ->  Seq Scan on t1
   ->  Hash
         ->  Seq Scan on t2
(5 rows)
```


In this query, we can see that an `IndexScan` is used in table `t2`. However, when `/*+Set(enable_indexscan off)*/` is used as a comment while executing this query, we can see that `SeqScan` is used in table `t2`. Users can combine any set of such GUC parameters in the hint phrases of their SQL queries. The list of available GUC parameters is enumerated in this [link](https://www.postgresql.org/docs/9.5/runtime-config-query.html).


### Feature 6: Hint tables

Embedding hint phrases in every query sometimes can be very overwhelming. Hence, `pg_hint_plan` provides users with the following feature. Users can group queries of similar type and instruct `pg_hint_plan` to enable the same hinting phrases for such queries. This information is stored in a table called `hint_plan.hints `which is maintained by the users themselves. The following example illustrates this in detail.


```sql
yugabyte=# INSERT INTO hint_plan.hints
(norm_query_string,
 application_name,
 hints)
VALUES
('EXPLAIN (COSTS false) SELECT * FROM t1 WHERE t1.id = ?;',
 '',
 'SeqScan(t1)');
INSERT 0 1
```

```sql
yugabyte=# INSERT INTO hint_plan.hints
(norm_query_string,
 application_name,
 hints)
VALUES
('EXPLAIN (COSTS false) SELECT id FROM t1 WHERE t1.id = ?;',
 '',
 'IndexScan(t1)');
INSERT 0 1
```

```sql
yugabyte=# select * from hint_plan.hints;
-[ RECORD 1 ]-----+--------------------------------------------------------
id                | 1
norm_query_string | EXPLAIN (COSTS false) SELECT * FROM t1 WHERE t1.id = ?;
application_name  |
hints             | SeqScan(t1)
-[ RECORD 2 ]-----+--------------------------------------------------------
id                | 2
norm_query_string | EXPLAIN (COSTS false) SELECT id FROM t1 WHERE t1.id = ?;
application_name  |
hints             | IndexScan(t1)
```


From this example, we see that users can insert into the hint_plan.hints table queries with placeholders for positional parameters using a question mark (?) symbol and their required hint phrases respectively. During runtime, when these queries are executed, `pg_hint_plan` automatically executes these queries with their respective hinting phrases.

To enable this feature users should follow two steps.



```sql
/* This steps creates the hint_plan.hints table*/
1. CREATE EXTENSION pg_hint_plan;

/*
 * This steps instructs pg_hint_plan to inspect hint tables to look for
 * hint phrases to be embedded along with the query.
 */
2. `SET pg_hint_plan.enable_hint_table = on;`
```
