---
title: EXPLAIN statement [YSQL]
headerTitle: EXPLAIN
linkTitle: EXPLAIN
description: Use the EXPLAIN statement to show the execution plan for an statement. If the ANALYZE option is used, the statement will be executed, rather than just planned.
menu:
  stable_api:
    identifier: perf_explain
    parent: statements
aliases:
  - /stable/api/ysql/commands/perf_explain/
rightnav:
  hideH4: true
type: docs
---

## Synopsis

Use the EXPLAIN statement to show the execution plan for a statement. If the ANALYZE option is used, the statement will be executed, rather than just planned. In that case, execution information (rather than just the planner's estimates) is added to the EXPLAIN result.

{{< warning title="DML vs DDL" >}}
The EXPLAIN statement is designed to work primarily for DML statements (for example, SELECT, INSERT, and so on). DDL statements are _not_ explainable and in cases where DDL and DML are combined, the EXPLAIN statement shows only an approximation. For example, EXPLAIN on `SELECT * FROM <TABLE-1> INTO <TABLE-2>` provides only an approximation as INTO is a DDL statement.
{{</ warning >}}

If you are using bucket-based scan optimizations, EXPLAIN output includes additional fields. Refer to [Bucket-based indexes](../../../../../develop/data-modeling/bucket-based-index-ysql/).

## Syntax

{{%ebnf%}}
  explain,
  option
{{%/ebnf%}}

## Semantics

Where statement is the target statement (see [SELECT](../dml_select/)).

### ANALYZE

Execute the statement and show actual run times and other statistics (default: `FALSE`).

### VERBOSE

Present more details about the plan, such as the output column list for each node in the plan tree, schema-qualify table and function names, ensure labeling variables in expressions with their range table alias, and consistently indicate the name of each trigger for which statistics are shown (default: `FALSE`).

### BUFFERS

Include information on buffer usage. Specifically, include the number of shared blocks hit, read, dirtied, and written; the number of local blocks hit, read, dirtied, and written; and the number of temporary blocks read and written (default: `FALSE`).

### COSTS

Incorporate details about the anticipated startup and total expenses for each plan node, along with the estimated number of rows and the projected width of each row (default: `ON`).

### DIST

Display additional runtime statistics related to the distributed storage layer as seen at the query layer. The flag also provides more implementation-specific insights into the distributed nature of query execution (default: `FALSE`).

### DEBUG

Display low-level runtime metrics related to [Cache and storage subsystems](../../../../../launch-and-manage/monitor-and-alert/metrics/cache-storage/#cache-and-storage-subsystems) (default: `FALSE`). When used with the DIST option, DEBUG provides both read and write metrics. These metrics are inlined within the query plan - read metrics appear under read operations (for example, under "Storage Table Read Requests"), and write metrics appear under write operations (for example, under "Storage Table Write Requests" or "Storage Flush Requests").

### FORMAT

Define the desired output format, choosing from TEXT, XML, JSON, or YAML. Non-text output retains the same information as the text format, but is more programmatically accessible (default: `TEXT`).

### SUMMARY

Display the overall timing and counters of the various execution nodes in the query execution plan (default: `TRUE`).

## Examples

Create a sample table:

```sql
CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

Add some data to the table:

```sql
INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

#### Simple select

```sql
EXPLAIN SELECT * FROM sample WHERE k1 = 1;
```

```yaml{.nocopy}
                                  QUERY PLAN
------------------------------------------------------------------------------
 Index Scan using sample_pkey on sample  (cost=0.00..15.25 rows=100 width=44)
   Index Cond: (k1 = 1)
```

#### Select with complex condition

```sql
EXPLAIN SELECT * FROM sample WHERE k1 = 2 and floor(k2 + 1.5) = v1;
```

```yaml{.nocopy}
                                  QUERY PLAN
------------------------------------------------------------------------------
 Index Scan using sample_pkey on sample  (cost=0.00..17.75 rows=100 width=44)
   Index Cond: (k1 = 2)
   Filter: (floor(((k2)::numeric + 1.5)) = (v1)::numeric)
```

#### Check execution with ANALYZE

```sql
EXPLAIN ANALYZE SELECT * FROM sample WHERE k1 = 2 and floor(k2 + 1.5) = v1;
```

```yaml{.nocopy}
                                                       QUERY PLAN
------------------------------------------------------------------------------------------------------------------------
 Index Scan using sample_pkey on sample  (cost=0.00..17.75 rows=100 width=44) (actual time=3.123..3.126 rows=1 loops=1)
   Index Cond: (k1 = 2)
   Filter: (floor(((k2)::numeric + 1.5)) = (v1)::numeric)
 Planning Time: 0.149 ms
 Execution Time: 3.198 ms
 Peak Memory Usage: 8 kB
```

#### Storage layer statistics

To view the request statistics, you can run with the DIST option as follows:

```sql
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF) SELECT * FROM sample WHERE k1 = 1;
```

```yaml{.nocopy}
                                    QUERY PLAN
----------------------------------------------------------------------------------
 Index Scan using sample_pkey on sample (actual time=3.999..4.013 rows=1 loops=1)
   Index Cond: (k1 = 1)
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 3.291 ms
   Storage Table Rows Scanned: 1
```

#### Internal RocksDB metrics

To view the internal metrics, you can run with the DEBUG option as follows:

```sql
EXPLAIN (ANALYZE, DIST, DEBUG, COSTS OFF, SUMMARY OFF) SELECT * FROM sample WHERE k1 = 1;
```

```yaml{.nocopy}
                                    QUERY PLAN
----------------------------------------------------------------------------------
 Index Scan using sample_pkey on sample (actual time=1.358..1.365 rows=1 loops=1)
   Index Cond: (k1 = 1)
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 0.963 ms
   Storage Table Rows Scanned: 1
   Metric rocksdb_number_db_seek: 1.000
   Metric rocksdb_number_db_next: 1.000
   Metric rocksdb_number_db_seek_found: 1.000
   Metric rocksdb_iter_bytes_read: 54.000
   Metric docdb_keys_found: 1.000
   Metric ql_read_latency: sum: 216.000, count: 1.000
```

For details on these metrics, see [Cache and storage subsystems](../../../../../launch-and-manage/monitor-and-alert/metrics/cache-storage/#cache-and-storage-subsystems).

#### Write operations with DEBUG

When using EXPLAIN with the DEBUG option on write operations (such as INSERT, UPDATE, or DELETE), write metrics are displayed inline under write-related fields. Read metrics continue to appear under read-related fields. The following example demonstrates this behavior:

```sql
CREATE TABLE a (k INT, v INT, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((100));

EXPLAIN (ANALYZE, DIST, DEBUG)
  WITH cte AS (
    INSERT INTO a VALUES (50, 50), (150, 150)
    RETURNING k
  )
  SELECT * FROM a, cte WHERE a.k = cte.k;
```

```yaml{.nocopy}
                                                     QUERY PLAN

---------------------------------------------------------------------------------------------------------------------

 YB Batched Nested Loop Join  (cost=0.03..4.30 rows=2 width=12) (actual time=4.758..4.773 rows=2 loops=1)
   Join Filter: (a.k = cte.k)
   CTE cte
     ->  Insert on a a_1  (cost=0.00..0.03 rows=2 width=8) (actual time=0.277..0.322 rows=2 loops=1)
           Storage Table Write Requests: 2
           ->  Values Scan on "*VALUES*"  (cost=0.00..0.03 rows=2 width=8) (actual time=0.002..0.004 rows=2 loops=1)
   ->  CTE Scan on cte  (cost=0.00..0.04 rows=2 width=4) (actual time=0.280..0.326 rows=2 loops=1)
   ->  Index Scan using a_pkey on a  (cost=0.00..2.11 rows=1 width=8) (actual time=4.197..4.206 rows=2 loops=1)
         Index Cond: (k = ANY (ARRAY[cte.k, $3, $4, ..., $1025]))
         Storage Table Read Requests: 1
         Storage Table Read Execution Time: 0.818 ms
         Storage Table Rows Scanned: 2
           Metric rocksdb_number_db_seek: 2.000
           Metric docdb_keys_found: 2.000
           Metric ql_read_latency: sum: 517.000, count: 2.000
         Storage Flush Requests: 1
         Storage Flush Execution Time: 2.829 ms
           Metric rocksdb_number_db_seek: 2.000
           Metric write_lock_latency: sum: 26.000, count: 2.000
           Metric ql_write_latency: sum: 2254.000, count: 2.000
 Planning Time: 25.192 ms
 Execution Time: 6.073 ms
 Storage Read Requests: 1
 Storage Read Execution Time: 0.818 ms
 Storage Rows Scanned: 2
   Metric rocksdb_number_db_seek: 2
   Metric docdb_keys_found: 2
   Metric ql_read_latency: sum: 517, count: 2
 Catalog Read Requests: 27
 Catalog Read Execution Time: 25.895 ms
 Catalog Write Requests: 0
 Storage Write Requests: 2
 Storage Flush Requests: 1
 Storage Flush Execution Time: 2.829 ms
   Metric rocksdb_number_db_seek: 2
   Metric write_lock_latency: sum: 26, count: 2
   Metric ql_write_latency: sum: 2254, count: 2
 Storage Execution Time: 29.542 ms
 Peak Memory Usage: 577 kB
(39 rows)
```

In this example, you can see that:

- Read metrics (such as `docdb_keys_found`, and `ql_read_latency`) appear under read operations like "Storage Table Read Requests".
- Write metrics (such as `write_lock_latency` and `ql_write_latency`) appear under write operations like "Storage Flush Requests".

## See also

- [INSERT](../dml_insert/)
- [SELECT](../dml_select/)
- [Analyze queries with EXPLAIN](../../../../../launch-and-manage/monitor-and-alert/query-tuning/explain-analyze/)
