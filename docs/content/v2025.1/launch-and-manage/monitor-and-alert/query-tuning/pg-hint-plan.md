---
title: Optimize YSQL queries using pg_hint_plan
linkTitle: Optimize YSQL queries
description: Query optimization of YSQL queries using pg_hint_plan
headerTitle: Optimize YSQL queries using pg_hint_plan
menu:
  v2025.1:
    identifier: pg_hint_plan
    parent: query-tuning
    weight: 600
type: docs
---

YugabyteDB leverages the PostgreSQL pg_hint_plan extension to control query execution plans with hinting phrases using a special form of comments.

YugabyteDB uses PostgreSQL's cost-based optimizer, which estimates the costs of each possible execution plan for an SQL statement. The execution plan with the lowest estimated cost is executed. The planner does its best to select the best execution plan, but is not perfect. Row counts play a crucial role in calculating the cost estimates and can be difficult to estimate. To overcome this and other limitations, you can use pg_hint_plan.

pg_hint_plan makes it possible to tweak execution plans using "hints", which are constraints on the query planner in the form of special SQL comments. These hints can specify a plan's join order, table access and join methods, and query-specific configuration parameters. In conjunction with the `plan hints table`, hints can be used to pin (lock down) a plan for a query.

{{< warning title="Revisit your hint plans" >}}

To use pg_hint_plan effectively, you need a thorough knowledge of how your application will be deployed. Hinted plans also need to be revisited when the database grows or the deployment changes to ensure that the plan is not limiting performance rather than optimizing it.

{{< /warning >}}

{{% explore-setup-single-new %}}

## Configure pg_hint_plan

pg_hint_plan is pre-configured, and enabled by default. The following YSQL configuration parameters control pg_hint_plan:

| Option | Description | Default |
| :----- | :---------- | :------ |
| `pg_hint_plan.enable_hint` | Turns pg_hint_plan on or off. | on |
| `pg_hint_plan.yb_bad_hint_mode` | Specifies the action taken if "bad" hints are specified.<br/>Valid values are `off`, `warn`, `replan`, and `error` | off |
| `pg_hint_plan.enable_hint_table` | Enable use of the hint table for storing/retrieving hints. | off |
| `pg_hint_plan.yb_use_query_id_for_hinting` | Use query IDs for storing/retrieving hints instead of query text. | off |
| `pg_hint_plan.hints_anywhere` | Allow the hint string to be placed anywhere in the query text. | off |
| `pg_hint_plan.debug_print` | Controls debug output.<br/>Valid values are `off` (no debug output), `on`, `detailed`, and `verbose`. | off |
| `pg_hint_plan.message_level` | Specifies the minimum message level for debug output.<br/>In *decreasing order of severity*, the levels are:<br/>`error`, `warning`, `notice`, `info`, `log`, and `debug`.<br/>Messages at the `fatal` and `panic` levels are always included in the output. | info |

### Enable pg_hint_plan

To enable pg_hint_plan, run the following command (if hints are not already enabled):

```sql
yugabyte=# SET pg_hint_plan.enable_hint=ON;
```

For more information on setting configuration parameters, refer to [Configuration parameters](../../../../reference/configuration/yb-tserver/#postgresql-configuration-parameters).

### Turn on debug output

To view the specific hints that pg_hint_plan uses and forwards to the query planner, turn on debug output. This is helpful for situations where syntactical errors or wrong hint names are present in hint phrases. Generally, debugging output does not need to be enabled if `pg_hint_plan.yb_bad_hint_mode` (see below) is set to a value other than OFF.

To view these debug prints, run the following commands:

```sql
yugabyte=# SET pg_hint_plan.debug_print TO on;
yugabyte=# \set SHOW_CONTEXT always
yugabyte=# SET client_min_messages TO log;
```

## Writing hint plans

pg_hint_plan parses hinting phrases of a special form present in SQL statements. This special form begins with the character sequence `/*+` and ends with `*/`. Hint phrases consist of hint names followed by hint parameters enclosed in parentheses and delimited by spaces. If `pg_hint_plan.hints_anywhere` is ON, the hint string can appear anywhere in the query string. If the value is OFF, the hint string can appear anywhere before the command keyword (SELECT, INSERT-SELECT, DELETE, UPDATE), *or* immediately after the command keyword:

- Valid : /*+ SeqScan(t1) */ EXPLAIN SELECT * FROM t1 WHERE id = 7;
- Valid : EXPLAIN /*+ SeqScan(t1) */ SELECT * FROM t1 WHERE id = 7;
- Valid : EXPLAIN SELECT /*+ SeqScan(t1) */ * FROM t1 WHERE id = 7;
- Invalid : EXPLAIN SELECT * /*+ SeqScan(t1) */ FROM t1 WHERE id = 7;

If hints are placed in an invalid location (with `pg_hint_plan.hints_anywhere` = OFF), the hint string is treated as a comment.

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

### Table identifiers in hints

Plan hints refer to FROM clause objects, tables (base or derived) in most cases. However, hints can also refer to table functions, common table expressions (CTEs), views (possibly nested), or VALUES clauses. If an alias is present for any of these entities, the alias should be used as the identifier. If no alias is present, the entity name (for example, a base table or view name) should be used in a hint. In the previous example, any hint would refer to 'a' or 'b', not 'pgbench_accounts' or 'pgbench_branches'.

Hints can refer to multiple query blocks (see [Hinting multiple blocks](#hinting-multiple-blocks)). Because the same name/alias can be used in multiple blocks, some way to reference these homonymous objects used in hints is required. Consider this example:

```sql
yugabyte=# EXPLAIN (COSTS off) SELECT COUNT(*) FROM t1, t2 WHERE t1.id = t2.id AND t1.val + t2.val = (SELECT MAX(t3.val) FROM t3, t1 WHERE t3.id
 = t2.id);
```

```output
                        QUERY PLAN
----------------------------------------------------------
 Aggregate
   ->  Hash Join
         Hash Cond: (t2.id = t1_1.id)
         Join Filter: ((t1_1.val + t2.val) = (SubPlan 1))
         ->  Seq Scan on t2
         ->  Hash
               ->  Seq Scan on t1 t1_1
         SubPlan 1
           ->  Aggregate
                 ->  Nested Loop
                       ->  Index Scan using t3_pkey on t3
                             Index Cond: (id = t2.id)
                       ->  Seq Scan on t1
(13 rows)
```

't1' appears twice, once in each of the 2 query blocks. However, in the EXPLAIN output one instance is aliased 't1_1', while the other instance has no alias. 't1_1' was not in the original SQL but instead was *generated as a unique identifer* for the instance of 't1' in the main query block. If multiple instances of an entity appear in a query, and you want to write hints referencing these entities, then you should use the generated unique alias.

An alternative approach is to rewrite the query and provide unique aliases. However, rewriting a query is often not possible, and in some cases rewriting the SQL to assign unique aliases can be very difficult and confusing.

### Set "bad hint" mode (pg_hint_plan.yb_bad_hint_mode)

Hints can be invalid (for example, trying to force use of an inapplicable or non-existent index), or can specify unsatisfiable conditions on the final plan (for example, trying to use hash join when there is no applicable equality condition). Historically, if invalid hints were given, a plan was still returned but it had a cost that has 'disable cost' (a very large value, 10e10) added to its final total cost. In many cases, it was difficult to ascertain what led to no satisfiable plan being found.

The settings to control what action to take in these situations are:

| Value | Action |
| :----- | :---- |
| OFF | Allow queries with bad hints to be planned even if hints have errors, are unused, or lead to undesired plans. A plan with a disabled cost may be found. |
| WARN | Issue a warning if any bad hints are found, and plan using hints where possible, that is, some hints may get used and others ignored. A plan with a disabled cost may be found. |
| REPLAN | Same warnings issued for bad hints as with WARN but drops hints and replans (with no hints). A plan with a disabled cost will cause replanning.|
| ERROR | Same messages issued as WARN but raises an error and no plan is produced. Any bad hint, or plan with disabled cost, will cause an error. |

The following is an example of a warning message (details on individual hint syntax is provided in later sections):

```sql
set pg_hint_plan.yb_bad_hint_mode to warn;
/*+ Hashjoin(t1 t2) */ explain (COSTS off, UIDS on) SELECT 1 FROM t1, t2 WHERE t1.id>t2.id;
```

```output
WARNING:  no valid method found for join with UID 5
WARNING:  unused hints, and/or hints causing errors, exist
             QUERY PLAN
------------------------------------
 Nested Loop (UID 5)
   Join Filter: (t1.id > t2.id)
   ->  Seq Scan on t1 (UID 1)
   ->  Materialize (UID 3)
         ->  Seq Scan on t2 (UID 2)
(5 rows)
```

For this query, a hash join of t1 and t2 is not possible (because there is no equality join condition) so a warning is issued. Notice that the cost of the resulting plan is greater than 10e10, that is, the disable cost was added to the join's cost as no plan satisfying the hints was found. Also note the use of the EXPLAIN option 'UIDS on', which displays a unique identifier for each node in the plan, and that the node with UID 5 is the problem.

The following is an example with a bad (non-existent) index hint:

```sql
set pg_hint_plan.yb_bad_hint_mode to warn;
/*+ Indexscan(t1 badindex) */ SELECT COUNT(*) FROM t1 WHERE id<5;
```

```output
WARNING:  bad index hint name "badindex" for table t1
WARNING:  unused hint: IndexScan(t1 badindex)
WARNING:  unused hints, and/or hints causing errors, exist
 count
-------
     4
(1 row)
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
| BitmapScan(table) | Enable BitmapScan on the table. |

In the following example, the hint `/*+ SeqScan(t2) */` forces table `t2` to be scanned using `SeqScan`.

``` sql
/*+ SeqScan(t1) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output

          QUERY PLAN
------------------------------
 Hash Join
   Hash Cond: (t1.id = t2.id)
   ->  Seq Scan on t1
   ->  Hash
         ->  Seq Scan on t2
(5 rows)
```

In the following example, due to the hint `/*+ SeqScan(t1) IndexScan(t2) */`, `t2` is scanned using IndexScan, and `t1` is scanned using `SeqScan`.

``` sql
/*+ SeqScan(t1) IndexScan(t2) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
                            QUERY PLAN
-------------------------------------------------------------------
 YB Batched Nested Loop Join
   Join Filter: (t1.id = t2.id)
   ->  Seq Scan on t1
   ->  Index Scan using t2_pkey on t2
         Index Cond: (id = ANY (ARRAY[t1.id, $1, $2, ..., $1023]))
(5 rows)
```

You can also use hint phrases to instruct the query planner not to use a specific type of scan. As shown in the following example, the hint `/*+ NoIndexScan(t1) */` prevents an `IndexScan` on table `t1`.

``` sql
/*+NoIndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
          QUERY PLAN
------------------------------
 Hash Join
   Hash Cond: (t1.id = t2.id)
   ->  Seq Scan on t1
   ->  Hash
         ->  Seq Scan on t2
(5 rows)
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
/*+ IndexScan(t3 t3_id2) */
EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
```

```output
          QUERY PLAN
-------------------------------
 Index Scan using t3_id2 on t3
   Index Cond: (id = 1)
(2 rows)
```

**Query reverts to `SeqScan` as an invalid index name is given**:

``` sql
set pg_hint_plan.yb_bad_hint_mode to warn;
/*+ IndexScan(t3 no_exist) */
EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
```

```output
/*+ IndexScan(t3 no_exist) */
EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
WARNING:  bad index hint name "no_exist" for table t3
WARNING:  unused hint: IndexScan(t3 no_exist)
WARNING:  unused hints, and/or hints causing errors, exist
           QUERY PLAN
--------------------------------
 Index Scan using t3_pkey on t3
   Index Cond: (id = 1)
(2 rows)
```

**Query with a selective list of indexes**:

``` sql
/*+ IndexScan(t3 t3_id1 t3_id2) */
EXPLAIN (COSTS false) SELECT * FROM t3 WHERE t3.id = 1;
```

```output
          QUERY PLAN
-------------------------------
 Index Scan using t3_id2 on t3
   Index Cond: (id = 1)
(2 rows)
```

In the preceding examples, the first query is executed with no hint phrase and the planner chooses an index scan using the primary key index `t3_pkey`. However, the second query contains the hint `/*+ IndexScan(t3 t3_id2) */`, and hence it uses the secondary index `t3_id2` to perform the index scan. When the hint phrase `/*+ IndexScan(t3 no_exist) */` is provided, a warning is issued and the planner reverts to `SeqScan` as the index does not exist. You can also provide a selective list of indices in the hint phrases, and the query planner chooses among them.

### Hints for join methods

Join method hints enforce the join methods for SQL statements. Using pg_hint_plan, you can specify the methodology by which tables should be joined. That is, for any logical join, a hint can be given to specify the physical join method to use for that logical join.

| Option | Value |
| :----- | :---- |
| HashJoin(t1 t2 t3 ...) | Join t1, t2, and t3 using HashJoin. |
| NoHashJoin(t1 t2 t3 ...) | Do not join t1, t2, and t3 using HashJoin.|
| MergeJoin(t1 t2 t3 ...) | Join t1, t2, and t3 using MergeJoin. |
| NoMergeJoin(t1 t2 t3 ...) | Do not join t1, t2, and t3 using MergeJoin.|
| NestLoop(t1 t2 t3 ...) | Join t1, t2, and t3 using NestLoop join. |
| NoNestLoop(t1 t2 t3 ...) | Do not join t1, t2, and t3 using NestLoop join.|
| YbBatchedNL(t1 t2 t3 ...) | Join t1, t2, and t3 using YbBatchedNL join.|
| NoYbBatchedNL(t1 t2 t3 ...) | Do not join t1, t2, and t3 using YbBatchedNL join.|

For example, this hint forces a hash join:

```sql
/*+ HashJoin(t1 t2) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
          QUERY PLAN
------------------------------
 Hash Join
   Hash Cond: (t1.id = t2.id)
   ->  Seq Scan on t1
   ->  Hash
         ->  Seq Scan on t2
(5 rows)
```

For the same query, this hint forces a nested loop join:

```sql
// Turn BNL off to simplify this example.
set yb_prefer_bnl to off;
set yb_enable_batchednl to off;
/*+ NestLoop(t1 t2) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
              QUERY PLAN
--------------------------------------
 Nested Loop
   ->  Seq Scan on t2
   ->  Index Scan using t1_pkey on t1
         Index Cond: (id = t2.id)
(4 rows)
```

The required join methods are specified in their respective hint phrases. You can use multiple hint phrases to combine join methods and scan methods.

The order of the tables in a join method hint is irrelevant. A join method hint with table aliases x, y, and z means 'If a (logical) join involving table aliases x, y, and z is found during the planner's join enumeration phase, use the specified join method.' So in the previous example, nested loop will be tried for both logical joins 'Join(t1 t2)' and 'Join(t2 t1)'. Control of the join order is described in [Hints for join order](#hints-for-join-order).

```sql
/*+ NestLoop(t2 t3 t1) SeqScan(t3) SeqScan(t2) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3
WHERE t1.id = t2.id AND t1.id = t3.id;
```

```output
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

In this example, the hint `/*+ NestLoop(t2 t3 t1) SeqScan(t3) SeqScan(t2) */` enables NestLoop join for any join involving tables t1, t2, and t3. It also enables `SeqScan` on tables t2 and t3 due to the hint phrases. The scan of t1 is performed using `IndexScan`, which is the access method chosen by the planner in the absence of a hint for t1.

For any set of hints specified for a query referencing table `t1`, there can be at most:

- 1 join method hint referencing `t1`
- 1 Leading hint referencing `t1` (see [Hints for join order](#hints-for-join-order))
- 1 table access method hint referencing `t1`

For example, the following is invalid:

```sql
/*+ NestLoop(t1 t2) MergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2
WHERE t1.id = t2.id;
INFO:  pg_hint_plan: hint syntax error at or near "NestLoop(t1 t2) MergeJoin(t1 t2)"
DETAIL:  Conflict join method hint.
```

This restriction includes 'negative' hints, for example, the following is also invalid:

```sql
EXPLAIN (COSTS false) SELECT /*+ MergeJoin(t1 t2) noHashJoin(t1 t2) */ * FROM t1, t2, t3 WHERE t1.id = t2.id AND t1.id =
t3.id;
```

```output
INFO:  pg_hint_plan: hint syntax error at or near "MergeJoin(t1 t2) noHashJoin(t1 t2) "
DETAIL:  Conflict join method hint.
```

### Hints for join order

The join ordering hint `Leading` forces execution of joins in a particular order, as enumerated in this hint's parameter list. The Leading hint has two forms:

1. Using nested parentheses. For example, `Leading(((t1 t2) t3))`.

    This form forces a single logical join order to be considered. In the example, t1-t2 must be the first join, with t1 as the left input. t1-t2 then must be joined to t3 with t3 as the right input. No join method was specified, so the planner is free to consider all join methods for the single logical join shape. If using this form, there must be a set of parentheses for each join, in addition to the closing parentheses. There are 2 joins in the example, so 2 sets of parentheses are required, plus the closing set.

1. Using no nested parentheses. For example, `Leading(t1 t2 t3)`.

    This form constrains the logical joins considered to *left-deep* trees starting with a join of the first 2 tables (reading left to right), followed by a join to the 3rd table, and so on. In the example, t1 is first joined to t2 *in any order*; that is, t1-t2 or t2-t1 is valid for each join method. Then t1-t2 must be joined to t3 in any order; that is, t3 can be the left or right input for each join method considered.

In the following example, form 1 is used, so the planner can only consider the join order `((t1 t2) t3)` for each join method:

```sql
/*+ Leading(((t1 t2) t3)) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t1.id = t3.id;
```

```output
             QUERY PLAN
------------------------------------
 Hash Join
   Hash Cond: (t1.id = t3.id)
   ->  Hash Join
         Hash Cond: (t1.id = t2.id)
         ->  Seq Scan on t1
         ->  Hash
               ->  Seq Scan on t2
   ->  Hash
         ->  Seq Scan on t3
(9 rows)
```

The following example uses form 2, so t1 had to be joined to t2 first but the planner had the choice of t1 or t2 as the left (right) input for each join method considered. (Hash join was chosen because it is the join method with the lowest estimated cost.)

```sql
/*+ Leading(t1 t2 t3) */
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3 WHERE t1.id = t2.id AND t1.id = t3.id;
```

```output
                               QUERY PLAN
-------------------------------------------------------------------------
 Hash Join
   Hash Cond: (t1.id = t3.id)
   ->  YB Batched Nested Loop Join
         Join Filter: (t1.id = t2.id)
         ->  Seq Scan on t2
         ->  Index Scan using t1_pkey on t1
               Index Cond: (id = ANY (ARRAY[t2.id, $1, $2, ..., $1023]))
   ->  Hash
         ->  Seq Scan on t3
(9 rows)
```

### Configuring the planner method

Planner method configuration parameters provide a crude method for influencing the query plans chosen by the query optimizer. If the default plan chosen by the optimizer for a particular query is not optimal, a temporary solution is to use one of these configuration parameters to force the optimizer to choose a different plan. YugabyteDB supports the following configuration parameters to allow:

- enable_bitmapscan
- enable_hashagg
- enable_hashjoin
- enable_indexscan
- enable_indexonlyscan
- enable_material
- enable_nestloop
- enable_mergejoin
- enable_partition_pruning
- enable_partitionwise_join
- enable_partitionwise_aggregate
- enable_seqscan
- enable_sort
- yb_prefer_bnl
- yb_enable_batchednl

Using these parameters is a coarse method to control plan choice, as setting one of them affects *all* joins in a query. For example, `enable_hashjoin(off)` prevents hash join from being considered for *all* joins in the query. Plan hints are a fine-grained mechanism to control join method choices for a specific join.

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
set pg_hint_plan.debug_print to on;
set client_min_messages to info;
set pg_hint_plan.message_level to info;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
```

```output
INFO:  pg_hint_plan:
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

The first query uses an `IndexScan` for table `t2`. However, when you add `/*+Set(enable_indexscan off)*/`, the second query uses `SeqScan` for table `t2`. You can combine any parameters in the hint phrases of your SQL queries.

For a more detailed explanation of the Planner Method Configuration and a complete list of available configuration parameters, refer to [Planner Method Configuration](https://www.postgresql.org/docs/15/runtime-config-query.html#RUNTIME-CONFIG-QUERY-ENABLE) in the PostgreSQL documentation.

## Using the hint table

Embedding hint phrases in every query can be overwhelming and, in many cases, is not possible. To help, pg_hint_plan can use a table called `hint_plan.hints` to store commonly-used hinting phrases. Using the hint table, you can group queries of similar type, and instruct pg_hint_plan to enable the same hinting phrases for all such queries.

To enable the hint table, run the following commands:

 ```sql
 /* Create the hint_plan.hints table */
CREATE EXTENSION IF NOT EXISTS pg_hint_plan;

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

yugabyte=# SELECT * FROM hint_plan.hints;
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

This example inserts queries into the `hint_plan.hints` table, with placeholders for positional parameters using a question mark (`?`) and their required hint phrases respectively. During runtime, when these queries are executed, `pg_hint_plan` automatically executes these queries with their respective hinting phrases.

However, for the hints to be used the query text string must match the stored query string *exactly* (except for the positional parameters, but matching *is* case-sensitive). For example:

```sql
yugabyte=# SET pg_hint_plan.debug_print TO on;
yugabyte=# \set SHOW_CONTEXT always
yugabyte=# SET client_min_messages TO info;
yugabyte=# SET pg_hint_plan.message_level TO info;
yugabyte=# EXPLAIN (COSTS false) SELECT * FROM t1 WHERE t1.id = 7;
```

```output
LOG:  pg_hint_plan:
used hint:
SeqScan(t1)
not used hint:
duplication hint:
error hint:

         QUERY PLAN
----------------------------
 Seq Scan on t1
   Storage Filter: (id = 7)
(2 rows)
```

The debugging output shows that the `SeqScan(t1)` hint is used. Similarly, for the second (`SELECT id`) you would see the following:

```sql
yugabyte=# EXPLAIN (COSTS false) SELECT id FROM t1 WHERE t1.id = 7;
```

```output
INFO:  pg_hint_plan:
used hint:
IndexScan(t1)
not used hint:
duplication hint:
error hint:

           QUERY PLAN
--------------------------------
 Index Scan using t1_pkey on t1
   Index Cond: (id = 7)
(2 rows)
```

However, if a space is inserted (after the WHERE clause) for the first query (`SELECT *`) the text will not match and the hint will not be used:

```sql
yugabyte=# EXPLAIN (COSTS false) SELECT * FROM t1 WHERE  t1.id = 7;
```

```output
           QUERY PLAN
--------------------------------
 Index Scan using t1_pkey on t1
   Index Cond: (id = 7)
(2 rows)
```

Nor will the hint be used for the query without EXPLAIN:

```sql
yugabyte=# SELECT * FROM t1 WHERE t1.id = 7;
```

```output
 id | val
----+-----
  7 |   7
(1 row)
```

To use hints for this query (without an EXPLAIN), you need to insert a new row in the hints table:

```sql
yugabyte=# INSERT INTO hint_plan.hints
(norm_query_string,
 application_name,
 hints)
VALUES
('SELECT * FROM t1 WHERE t1.id = ?;',
 '',
 'SeqScan(t1)');

INSERT 0 1

yugabyte=# SELECT * FROM t1 WHERE t1.id = 7;
```

```output
LOG:  pg_hint_plan:
used hint:
SeqScan(t1)
not used hint:
duplication hint:
error hint:

 id | val
----+-----
  7 |   7
(1 row)
```

Inserting query text with positional parameters can be difficult and error-prone. An alternative is to use `query id` instead of query text. Each query processed by the planner is assigned an ID that is computed from the query's characterisics. To see a query's ID, simply run a verbose EXPLAIN. For example (with the WHERE clause condition on `val` instead of `id`):

```sql
yugabyte=# EXPLAIN (VERBOSE true, COSTS off) SELECT * FROM t1 WHERE t1.val = 9;
```

```output
               QUERY PLAN
----------------------------------------
 Index Scan using t1_val on public.t1
   Output: id, val
   Index Cond: (t1.val = 9)
 Query Identifier: -6731874999214575733
(4 rows)
```

To use the hint table to store a hint for this query, set `pg_hint_plan.yb_use_query_id_for_hinting` to ON and simply use the query ID instead of query text when inserting into the hint plan table:

```sql
yugabyte=# SET pg_hint_plan.yb_use_query_id_for_hinting to ON;
yugabyte=# INSERT INTO hint_plan.hints
(norm_query_string,
application_name,
hints)
VALUES
('-6731874999214575733',
'',
'SeqScan(t1)');

INSERT 0 1

yugabyte=# SELECT norm_query_string, hints FROM hint_plan.hints ORDER BY id;
```

```output
                    norm_query_string                     |     hints
----------------------------------------------------------+---------------
 EXPLAIN (COSTS false) SELECT * FROM t1 WHERE t1.id = ?;  | SeqScan(t1)
 EXPLAIN (COSTS false) SELECT id FROM t1 WHERE t1.id = ?; | IndexScan(t1)
 SELECT * FROM t1 WHERE t1.id = ?;                        | SeqScan(t1)
 -6731874999214575733                                     | SeqScan(t1)
(4 rows)
```

Now when the query is re-run you will see a sequential scan used for `t1` instead of index scan:

```sql
yugabyte=# EXPLAIN (VERBOSE true, COSTS off) SELECT * FROM t1 WHERE t1.val = 9;
```

```output
INFO:  pg_hint_plan:
used hint:
SeqScan(t1)
not used hint:
duplication hint:
error hint:

               QUERY PLAN
----------------------------------------
 Seq Scan on public.t1
   Output: id, val
   Storage Filter: (t1.val = 9)
 Query Identifier: -6731874999214575733
(4 rows)
```

Query IDs are computed using the planner's internal query representation, so the query ID for the EXPLAIN and non-EXPLAIN versions are the same. This means that for the previous query without EXPLAIN will use the same plan as the EXPLAIN version:

```sql
yugabyte=# SELECT * FROM t1 WHERE t1.val = 9;
```

```output
INFO:  pg_hint_plan:
used hint:
SeqScan(t1)
not used hint:
duplication hint:
error hint:

  id  | val
------+-----
 8109 |   9
 3909 |   9
 1909 |   9
 ...  | ...
(1 row)
```

Additionally, query ID calculation is not sensitive to spaces or comments:

```sql
yugabyte=# SELECT *   /* This is a comment. */  FROM t1 WHERE     t1.val     = 9;
```

```output
INFO:  pg_hint_plan:
used hint:
SeqScan(t1)
not used hint:
duplication hint:
error hint:

  id  | val
------+-----
 8109 |   9
 3909 |   9
 1909 |   9
 ...  | ...
```

Query IDs are calculated using internal table identifiers, not table names. As a consequence, dropping and recreating a table will lead to a different query ID being calculated for queries that reference the table.

## Hinting multiple blocks

Hints can be specified to control table and join methods in multiple blocks. Consider this query and plan:

```sql
yugabyte=# EXPLAIN (COSTS off) SELECT COUNT(*) FROM t1, t2 WHERE t1.id = t2.id AND t1.val + t2.val = (SELECT MAX(t3.val) FROM t3, t1 WHERE t3.id=t1.id AND t1.val=10) AND t1.val=9;
```

```output
                                   QUERY PLAN
---------------------------------------------------------------------------------
 Aggregate
   InitPlan 1 (returns $1024)
     ->  Aggregate
           ->  YB Batched Nested Loop Join
                 Join Filter: (t3.id = t1.id)
                 ->  Index Scan using t1_val on t1
                       Index Cond: (val = 10)
                 ->  Index Scan using t3_pkey on t3
                       Index Cond: (id = ANY (ARRAY[t1.id, $1, $2, ..., $1023]))
   ->  Nested Loop
         ->  Index Scan using t1_val on t1 t1_1
               Index Cond: (val = 9)
         ->  Index Scan using t2_pkey on t2
               Index Cond: (id = t1_1.id)
               Storage Filter: ((t1_1.val + val) = $1024)
(15 rows)
```

Suppose you would like to change:

1. NestedLoop(t1_1 t2) to MergeJoin(t2 t1_1).
2. YbBatchedNestedLoopJoin(t3 t1) to HashJoin(t1 t3).
3. IndexScan(t1) to SeqScan(t1).

To accomplish 1 and 2, you need to change both the join orders and the join methods. The set of hints to accomplish this is:

```sql
yugabyte=# /*+ Leading((t2 t1_1)) Leading((t1 t3)) MergeJoin(t1_1 t2) HashJoin(t1 t3) SeqScan(t1) */ EXPLAIN (COSTS off) SELECT COUNT(*) FROM t1
, t2 WHERE t1.id = t2.id AND t1.val + t2.val = (SELECT MAX(t3.val) FROM t3, t1 WHERE t3.id=t1.id AND t1.val=10) AND t1.val=9;
```

```output
INFO:  pg_hint_plan:
used hint:
SeqScan(t1)
HashJoin(t1 t3)
MergeJoin(t1_1 t2)
Leading((t2 t1_1))
Leading((t1 t3))
not used hint:
duplication hint:
error hint:

                      QUERY PLAN
------------------------------------------------------
 Aggregate
   InitPlan 1 (returns $0)
     ->  Aggregate
           ->  Hash Join
                 Hash Cond: (t1.id = t3.id)
                 ->  Seq Scan on t1
                       Storage Filter: (val = 10)
                 ->  Hash
                       ->  Seq Scan on t3
   ->  Merge Join
         Merge Cond: (t2.id = t1_1.id)
         Join Filter: ((t1_1.val + t2.val) = $0)
         ->  Sort
               Sort Key: t2.id
               ->  Seq Scan on t2
         ->  Sort
               Sort Key: t1_1.id
               ->  Index Scan using t1_val on t1 t1_1
                     Index Cond: (val = 9)
(19 rows)
```

Two (strict) Leading hints are used along with two join method hints, and the SeqScan hint only affects one instance of `t1`, as expected.

## Generating hints

The `HINTS on` EXPLAIN option allows you to see the set of plan hints that would generate the plan the optimizer found for a query. For example, again consider the previous query's plan (using no hints), this time using `HINTS on`:

```sql
yugabyte=# EXPLAIN (COSTS off, HINTS on, VERBOSE on) SELECT COUNT(*) FROM t1, t2 WHERE t1.id = t2.id AND t1.val + t2.val = (SELECT MAX(t3.val) FROM t3, t1 WHERE t3.id=t1.id AND t1.val=10) AND t1.val=9;
```

```output
                                        QUERY PLAN
------------------------------------------------------------------------------------------------
 Aggregate
   Output: count(*)
   InitPlan 1 (returns $1024)
     ->  Aggregate
           Output: max(t3.val)
           ->  YB Batched Nested Loop Join
                 Output: t3.val
                 Inner Unique: true
                 Join Filter: (t3.id = t1.id)
                 ->  Index Scan using t1_val on public.t1
                       Output: t1.id
                       Index Cond: (t1.val = 10)
                 ->  Index Scan using t3_pkey on public.t3
                       Output: t3.val, t3.id
                       Index Cond: (t3.id = ANY (ARRAY[t1.id, $1, $2, $3, ...]))
   ->  Nested Loop
         Inner Unique: true
         ->  Index Scan using t1_val on public.t1 t1_1
               Output: t1_1.id, t1_1.val
               Index Cond: (t1_1.val = 9)
         ->  Index Scan using t2_pkey on public.t2
               Output: t2.id, t2.val
               Index Cond: (t2.id = t1_1.id)
               Storage Filter: ((t1_1.val + t2.val) = $1024)
 Query Identifier: -7881676297850663726
 Generated hints: /*+ Leading((t1_1 t2)) IndexScan(t1_1 t1_val) IndexScan(t2 t2_pkey) NestLoop(t1_1 t2) Leading((t1 t3)) IndexScan(t1 t1_val) IndexScan(t3 t3_pkey) YbBatchedNL(t1 t3) Set(yb_enable_optimizer_statistics off) Set(yb_enable_base_scans_cost_model off) Set(enable_hashagg on) Set(enable_material on) Set(enable_memoize on) Set(enable_sort on) Set(enable_incremental_sort on) Set(max_parallel_workers_per_gather 2) Set(parallel_tuple_cost 0.10) Set(parallel_setup_cost 1000.00) Set(min_parallel_table_scan_size 1024) Set(yb_prefer_bnl on) Set(yb_bnl_batch_size 1024) Set(yb_fetch_row_limit 1024) Set(from_collapse_limit 8) Set(join_collapse_limit 8) Set(geqo false) */
(26 rows)
```

The EXPLAIN output now contains a new line `Generated hints`. This is the set of hints, which if specified in conjunction with the query, will lead to the *exact same plan* in terms of join order, and join and table access methods. The SET options capture the relevant configuration options the planner used when searching for the best plan. To ensure the exact same plan is always given for this query, you can store the hints in the hint table using the query ID `-7881676297850663726`.

The generated hints also provide a useful template for devising a new set of hints to change the plan. Writing a new set of hints to constrain the plan for a complex query can be difficult. To see this, consider a query of the view 'information_schema.columns':

```sql
EXPLAIN (HINTS on, COSTS off) SELECT * FROM information_schema.columns;
```

```output
                                        QUERY PLAN
------------------------------------------------------------------------------------------------
  Nested Loop Left Join
   Join Filter: ((dep.refobjid = c.oid) AND (dep.refobjsubid = a.attnum))
   ->  Merge Right Join
         Merge Cond: (co.oid = a.attcollation)
         ->  Nested Loop
               Join Filter: ((co.collnamespace = nco.oid) AND ((nco.nspname <> 'pg_catalog'::name) OR (co.collname <> 'default'::name)))
               ->  Index Scan using pg_collation_oid_index on pg_collation co
               ->  Materialize
                     ->  Seq Scan on pg_namespace nco
         ->  Sort
               Sort Key: a.attcollation
               ->  Hash Join
                     Hash Cond: (t.typnamespace = nt.oid)
                     ->  YB Batched Nested Loop Left Join
                           Join Filter: ((t.typtype = 'd'::"char") AND (t.typbasetype = bt.oid))
                           ->  YB Batched Nested Loop Join
                                 Join Filter: (a.atttypid = t.oid)
                                 ->  Hash Left Join
                                       Hash Cond: ((a.attrelid = ad.adrelid) AND (a.attnum = ad.adnum))
                                       ->  Hash Join
                                             Hash Cond: (a.attrelid = c.oid)
                                             Join Filter: (pg_has_role(c.relowner, 'USAGE'::text) OR has_column_privilege(c.oid, a.attnum, 'SELECT, INSERT, UPDATE, REFERENCES'::text))
                                             ->  Index Scan using pg_attribute_relid_attnum_index on pg_attribute a
                                                   Index Cond: (attnum > 0)
                                                   Storage Filter: (NOT attisdropped)
                                             ->  Hash
                                                   ->  YB Batched Nested Loop Join
                                                         Join Filter: (c.relnamespace = nc.oid)
                                                         ->  Seq Scan on pg_namespace nc
                                                               Filter: (NOT pg_is_other_temp_schema(oid))
                                                         ->  Index Scan using pg_class_relname_nsp_index on pg_class c
                                                               Index Cond: (relnamespace = ANY (ARRAY[nc.oid, $1, $2, ..., $1023]))
                                                               Storage Filter: (relkind = ANY ('{r,v,f,p}'::"char"[]))
                                       ->  Hash
                                             ->  Seq Scan on pg_attrdef ad
                                 ->  Memoize
                                       Cache Key: a.atttypid
                                       Cache Mode: logical
                                       ->  Index Scan using pg_type_oid_index on pg_type t
                                             Index Cond: (oid = ANY (ARRAY[a.atttypid, $1025, $1026, ..., $2047]))
                           ->  YB Batched Nested Loop Join
                                 Join Filter: (bt.typnamespace = nbt.oid)
                                 Sort Keys: bt.oid
                                 ->  Index Scan using pg_type_oid_index on pg_type bt
                                       Index Cond: (oid = ANY (ARRAY[t.typbasetype, $2049, $2050, ..., $3071]))
                                 ->  Index Scan using pg_namespace_oid_index on pg_namespace nbt
                                       Index Cond: (oid = ANY (ARRAY[bt.typnamespace, $3073, $3074, ..., $4095]))
                     ->  Hash
                           ->  Seq Scan on pg_namespace nt
   ->  Materialize
         ->  Hash Join
               Hash Cond: (dep.objid = seq.seqrelid)
               ->  Index Scan using pg_depend_reference_index on pg_depend dep
                     Index Cond: (refclassid = '1259'::oid)
                     Storage Filter: ((classid = '1259'::oid) AND (deptype = 'i'::"char"))
               ->  Hash
                     ->  Seq Scan on pg_sequence seq
 Generated hints: /*+ Leading((((co nco) (((((a (nc c)) ad) t) (bt nbt)) nt)) (dep seq))) IndexScan(co pg_collation_oid_index) SeqScan(nco) NestLoop(co nco) IndexScan(a pg_attribute_relid_attnum_index) SeqScan(nc) IndexScan(c pg_class_relname_nsp_index) YbBatchedNL(c nc) HashJoin(a c nc) SeqScan(ad) HashJoin(a ad c nc) IndexScan(t pg_type_oid_index) YbBatchedNL(a ad c nc t) IndexScan(bt pg_type_oid_index) IndexScan(nbt pg_namespace_oid_index) YbBatchedNL(bt nbt) YbBatchedNL(a ad bt c nbt nc t) SeqScan(nt) HashJoin(a ad bt c nbt nc nt t) MergeJoin(a ad bt c co nbt nc nco nt t) IndexScan(dep pg_depend_reference_index) SeqScan(seq) HashJoin(dep seq) NestLoop(a ad bt c co dep nbt nc nco nt seq t) Set(yb_enable_optimizer_statistics off) Set(yb_enable_base_scans_cost_model off) Set(enable_hashagg on) Set(enable_material on) Set(enable_memoize on) Set(enable_sort on) Set(enable_incremental_sort on) Set(max_parallel_workers_per_gather 2) Set(parallel_tuple_cost 0.10) Set(parallel_setup_cost 1000.00) Set(min_parallel_table_scan_size 1024) Set(yb_prefer_bnl on) Set(yb_bnl_batch_size 1024) Set(yb_fetch_row_limit 1024) Set(from_collapse_limit 12) Set(join_collapse_limit 12) Set(geqo false) */
(58 rows)
```

This hint set is complex and difficult to write from scratch. Using the generated hint set as a template makes this much easier and less error-prone.

## Learn more

- Refer to [Get query statistics using pg_stat_statements](../pg-stat-statements/) to track planning and execution of all the SQL statements.
- Refer to [View live queries with pg_stat_activity](../../../../explore/observability/pg-stat-activity/) to analyze live queries.
- Refer to [View COPY progress with pg_stat_progress_copy](../../../../explore/observability/pg-stat-progress-copy/) to track the COPY operation status.
- Refer to [Analyze queries with EXPLAIN](../explain-analyze/) to optimize YSQL's EXPLAIN and EXPLAIN ANALYZE queries.
