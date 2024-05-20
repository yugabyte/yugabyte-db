---
title: Optimize YSQL queries using pg_hint_plan
linkTitle: Optimize YSQL queries
description: Query optimization of YSQL queries using pg_hint_plan
headerTitle: Optimize YSQL queries using pg_hint_plan
menu:
  stable:
    identifier: pg_hint_plan
    parent: query-tuning
    weight: 600
type: docs
---

YugabyteDB leverages the PostgreSQL pg_hint_plan extension to control query execution plans with hinting phrases using comments.

YugabyteDB uses PostgreSQL's cost-based optimizer, which estimates the costs of each possible execution plan for an SQL statement. The execution plan with the lowest cost is executed. The planner does its best to select the best execution plan, but is not perfect. Additionally, the version of the planner used by YugabyteDB is sub-optimal. For instance, the cost-based optimizer is naive and assumes row counts for all tables to be 1000. Row counts however play a crucial role in calculating the cost estimates. To overcome these limitations, you can use pg_hint_plan.

pg_hint_plan makes it possible to tweak execution plans using "hints", which are descriptions in the form of SQL comments.

{{< warning title="Revisit your hint plans" >}}

To use `pg_hint_plan` effectively, you need thorough knowledge of how your application will be deployed. Hint plans also need to be revisited when the database grows or the deployment changes to ensure that the plan is not limiting performance rather than optimizing it.

{{< /warning >}}

{{% explore-setup-single %}}

## Configure pg_hint_plan

pg_hint_plan is pre-configured, and enabled by default. The following YSQL configuration parameters control pg_hint_plan:

| Option | Description | Default |
| :----- | :---------- | :------ |
| `pg_hint_plan.enable_hint` | Turns pg_hint_plan on or off. | on |
| `pg_hint_plan.debug_print` | Controls debug output.<br/>Valid values are `off` (no debug output), `on`, `detailed`, and `verbose`. | off |
| `pg_hint_plan.message_level` | Specifies the minimum message level for debug output.<br/>In _decreasing order of severity_, the levels are:<br/>`error`, `warning`, `notice`, `info`, `log`, and `debug`.<br/>Messages at the `fatal` and `panic` levels are always included in the output. | info |

### Enable pg_hint_plan

To enable pg_hint_plan, run the following command:

```sql
yugabyte=# SET pg_hint_plan.enable_hint=ON;
```

{{<note title="Enable pg_hint_plan for all sessions">}}
You can enable `pg_hint_plan` in different levels like [all PostgreSQL options can](../../../reference/configuration/yb-tserver/#postgresql-server-options).
{{</note>}}


### Turn on debug output

To view the specific hints that pg_hint_plan uses and forwards to the query planner, turn on debug output. This is helpful for situations where syntactical errors or wrong hint names are present in hint phrases. To view these debug prints, run the following commands:

```sql
yugabyte=# SET pg_hint_plan.debug_print TO on;
yugabyte=# \set SHOW_CONTEXT always
yugabyte=# SET client_min_messages TO log;
```

## Writing hint plans

pg_hint_plan parses hinting phrases of a special form present in SQL statements. This special form begins with the character sequence `/*+` and ends with `*/`. Hint phrases consist of hint names followed by hint parameters enclosed in parentheses and delimited by spaces.

In the following example, `HashJoin` is selected as the joining method for joining `pg_bench_branches` and `pg_bench_accounts` and a `SeqScan` is used for scanning the table `pgbench_accounts`.

```sql
yugabyte=# /*+
yugabyte*#    HashJoin(a b)
yugabyte*#    SeqScan(a)
yugabyte*#  */
yugabyte-# EXPLAIN SELECT *
yugabyte-#    FROM pgbench_branches b
yugabyte-#    JOIN pgbench_accounts a ON b.bid = a.bid
yugabyte-#   ORDER BY a.aid;
```

```output
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

## Using pg_hint_plan

The following table and index definitions are used in the examples that follow to illustrate the features of pg_hint_plan:

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

The schema of the resulting tables is as follows:

```output
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

### Hints for scan methods

Scan method hints enforce the scanning method on tables when specified along with appropriate hint phrases. You should specify your required scan method and respective target tables by alias names if any. YugabyteDB supports the following scan methods with hint phrases from pg_hint_plan:

| Option | Value |
| :----- | :---- |
| SeqScan(table) | Enable SeqScan on the table. |
| NoSeqScan(table) | Do not enable SeqScan on the table. |
| IndexScan(table) | Enable IndexScan on the table. |
| IndexScan(table idx) | Enable IndexScan on the table using the index `idx`. |
| NoIndexScan(table) | Do not enable IndexScan on the table. |
| IndexOnlyScan(table) | Enable IndexOnlyScan on the table. |
| NoIndexOnlyScan(table) | Do not enable IndexOnlyScan on the table. |
| IndexScanRegexp(table regex) | Enable index scan on the table whose indices match with the regular expression defined by `regex`. |
| IndexOnlyScanRegexp(table regex) | Do not enable index scan on the table whose indices match with the regular expression defined by `regex`. |

In the following example, the hint `/*+SeqScan(t2)*/` allows table `t2` to be scanned using `SeqScan`.

``` sql
/*+SeqScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
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

In the following example, due to the hint `/*+SeqScan(t1)IndexScan(t2)*/`, `t2` is scanned using IndexScan, and `t1` is scanned using `SeqScan`.

``` sql
/*+SeqScan(t1)IndexScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
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

You can also use hint phrases to instruct the query planner not to use a specific type of scan. As shown in the following example, the hint `/*+NoIndexScan(t1)*/` restricts `IndexScan` on table `t1`.

``` sql
/*+NoIndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
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

### Specifying indices in hint phrases

A single table can have many indices. Using pg_hint_plan, you can specify the exact index to use while performing scans on a table. Consider table `t3` as an example. It contains multiple indices for the column `id` (`t3_pkey, t3_id1, t3_id2, t3_id3`). You can choose any of these indices to run scans by executing the SQL queries with the appropriate hint phrases.

**Query without a hint phrase**:

``` sql
yugabyte=# EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
```

```output
           QUERY PLAN
--------------------------------
 Index Scan using t3_pkey on t3
   Index Cond: (id = 1)
(2 rows)
```

**Query using a secondary index**:

``` sql
/*+IndexScan(t3 t3_id2)*/
EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
```

```output
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

**Query reverts to `SeqScan` as none of the indices can be used**:

``` sql
/*+IndexScan(t3 no_exist)*/
EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
```

```output
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

**Query with a selective list of indexes**:

``` sql
/*+IndexScan(t3 t3_id1 t3_id2)*/
EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
```

```output
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

In the preceding examples, the first query is executed with no hint phrase. Hence, the index scan uses the primary key index `t3_pkey`. However, the second query contains the hint `/*+IndexScan(t3 t3_id2)*/`, and hence it uses the secondary index `t3_id2` to perform the index scan. When the hint phrase `/*+IndexScan(t3 no_exist)*/` is provided, the planner reverts to `SeqScan` as none of the indices can be used. You can also provide a selective list of indices in the hint phrases, and the query planner chooses among them.

### Hints for join methods

Join method hints enforce the join methods for SQL statements. Using pg_hint_plan, you can specify the methodology by which tables should be joined.

| Option | Value |
| :----- | :---- |
| HashJoin(t1 t2 t3 ...) | Join t1, t2, and t3 using HashJoin. |
| NoHashJoin(t1 t2 t3 ...) | Do not join t1, t2, and t3 using HashJoin.|
| NestLoop(t1 t2 t3 ...) | Join t1, t2, and t3 using NestLoop join. |
| NoNestLoop(t1 t2 t3 ...) | Do not join t1, t2, and t3 using NestLoop join.|
| [YbBatchedNL](../../ysql-language-features/join-strategies/#batched-nested-loop-join-bnl)(t1 t2) | Join t1 and t2 using YbBatchedNL join.|

```sql
/*+HashJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
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
```

```output
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

In this example, the first query uses a `HashJoin` on tables `t1` and `t2` respectively while the second query uses a `NestedLoop` join for the same. The required join methods are specified in their respective hint phrases. You can use multiple hint phrases to combine join methods and scan methods.

```sql
/*+NestLoop(t2 t3 t1) SeqScan(t3) SeqScan(t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3
WHERE t1.id = t2.id AND t1.id = t3.id;
```

```output
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

In the preceding example, the hint `/*+NestLoop(t2 t3 t4 t1) SeqScan(t3) SeqScan(t4)*/` enables NestLoop join on tables t1, t2, t3, and t4. It also enables `SeqScan` on tables t3 and t4 due to the hint phrases. The rest of the scans are performed using `IndexScan`.

### Hints for joining order

Joining order hints execute joins in a particular order, as enumerated in a hint phrase's parameter list. You can enforce joining in a specific order using the `Leading` hint.

```sql
/*+Leading(t1 t2 t3)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t1.id = t3.id;
```

```output
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
/*+Leading(t2 t3 t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t1.id = t3.id;
```

```output
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

The joining order in the first query is `/*+Leading(t1 t2 t3)*/`, whereas the joining order for the second query is `/*+Leading(t2 t3 t1)*/`. You can see that the query plan's order of execution follows the joining order specified in the parameter lists of the hint phrases.

### Configuring the planner method

Planner method configuration parameters provide a crude method of influencing the query plans chosen by the query optimizer. If the default plan chosen by the optimizer for a particular query is not optimal, a temporary solution is to use one of these configuration parameters to force the optimizer to choose a different plan. YugabyteDB supports the following configuration parameters:

- enable_hashagg
- enable_hashjoin
- enable_indexscan
- enable_indexonlyscan
- enable_material
- enable_nestloop
- enable_partition_pruning
- enable_partitionwise_join
- enable_partitionwise_aggregate
- enable_seqscan
- enable_sort

pg_hint_plan leverages the planner method configuration by embedding these configuration parameters in each query's comment. Consider the following example:

```sql
yugabyte=# EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Seq Scan on t1
   ->  Index Scan using t2_pkey on t2
         Index Cond: (id = t1.id)
(4 rows)
```

```sql
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
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

The first query uses an `IndexScan` in table `t2`. However, when you add `/*+Set(enable_indexscan off)*/`, the second query uses `SeqScan` in table `t2`. You can combine any parameters in the hint phrases of your SQL queries.

For a more detailed explanation of the Planner Method Configuration and a complete list of available configuration parameters, refer to [Planner Method Configuration](https://www.postgresql.org/docs/11/runtime-config-query.html#RUNTIME-CONFIG-QUERY-ENABLE) in the PostgreSQL documentation.

## Using the hint table

Embedding hint phrases in every query can be overwhelming. To help, pg_hint_plan can use a table called `hint_plan.hints` to store commonly-used hinting phrases. Using the hint table, you can group queries of similar type, and instruct pg_hint_plan to enable the same hinting phrases for all such queries.

To enable the hint table, run the following commands:

 ```sql
 /* Create the hint_plan.hints table */
CREATE EXTENSION pg_hint_plan;

 /*
  * Tell pg_hint_plan to check the hint table for
  * hint phrases to be embedded along with the query.
  */
SET pg_hint_plan.enable_hint_table = on;
 ```

The following example illustrates this in detail.

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

yugabyte=# INSERT INTO hint_plan.hints
(norm_query_string,
 application_name,
 hints)
VALUES
('EXPLAIN (COSTS false) SELECT id FROM t1 WHERE t1.id = ?;',
 '',
 'IndexScan(t1)');

INSERT 0 1

yugabyte=# select * from hint_plan.hints;
```

```output
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

This example inserts queries into the `hint_plan.hints` table, with placeholders for positional parameters using a question mark (?) and their required hint phrases respectively. During runtime, when these queries are executed, `pg_hint_plan` automatically executes these queries with their respective hinting phrases.

## Learn more

- Refer to [Get query statistics using pg_stat_statements](../pg-stat-statements/) to track planning and execution of all the SQL statements.
- Refer to [View live queries with pg_stat_activity](../../observability/pg-stat-activity/) to analyze live queries.
- Refer to [View COPY progress with pg_stat_progress_copy](../../observability/pg-stat-progress-copy/) to track the COPY operation status.
- Refer to [Analyze queries with EXPLAIN](../explain-analyze/) to optimize YSQL's EXPLAIN and EXPLAIN ANALYZE queries.
