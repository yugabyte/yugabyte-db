---
title: ANALYZE statement [YSQL]
headerTitle: ANALYZE
linkTitle: ANALYZE
description: Collect statistics about database tables with the ANALYZE statement.
tags:
  feature: early-access
menu:
  v2.25_api:
    identifier: cmd_analyze
    parent: statements
type: docs
---

## Synopsis

ANALYZE collects statistics about the contents of tables in the database, and stores the results in the [pg_statistic](../../../../../architecture/system-catalog/#data-statistics), [pg_class](../../../../../architecture/system-catalog/#schema), and [pg_stat_all_tables](../../../../../architecture/system-catalog/#table-activity) system catalogs. These statistics help the query planner to determine the most efficient execution plans for queries.

The statistics are also used by the YugabyteDB [cost based optimizer](../../../../../architecture/query-layer/planner-optimizer) (CBO) to create optimal execution plans for queries. When run on up-to-date statistics, CBO provides performance improvements and can reduce or eliminate the need to use hints or modify queries to optimize query execution.

{{< warning title="Run ANALYZE regularly" >}}
If you have enabled CBO, you must run ANALYZE on user tables after data load for the CBO to create optimal execution plans.

You can automate running ANALYZE using the [Auto Analyze service](../../../../../additional-features/auto-analyze/).
{{< /warning >}}

The YugabyteDB implementation is based on the framework provided by PostgreSQL, which requires the storage layer to provide a random sample of rows of a predefined size. The size is calculated based on a number of factors, such as the included columns' data types.

{{< note title="Large tables" >}}
The sampling algorithm is not currently optimized for large tables. It may take several minutes to collect statistics from a table containing millions of rows of data.
{{< /note >}}

## Syntax

{{%ebnf%}}
  analyze,
  table_and_columns
{{%/ebnf%}}

## Semantics

### VERBOSE

Enable display of progress messages.

### *table_name*

Table name to be analyzed; may be schema-qualified. Optional. Omit to analyze all regular tables in the current database.

### *column_name*

List of columns to be analyzed. Optional. Omit to analyze all columns of the table.

## Reset statistics

Over time, statistics can reach a point where they no longer represent the current workload accurately. Resetting allows you to measure the impact of recent changes, like optimizations or new queries, without the influence of historical data. Also, when diagnosing issues, fresh statistics can help pinpoint current issues more effectively, rather than having to sift through historical data that may not be relevant.

The `yb_reset_analyze_statistics()` function is a convenient helper that offers an easy way to clear statistics collected for a specific table or for all tables in a database. Call this function as follows:

```sql
SELECT yb_reset_analyze_statistics ( table_oid );
```

If table_oid is NULL, this function resets the statistics for all the tables in the current database that the user can analyze.

## Examples

### Analyze a single table

```plpgsql
yugabyte=# ANALYZE some_table;
```

```output
ANALYZE
```

### Analyze specific columns

```plpgsql
yugabyte=# ANALYZE some_table(col1, col3);
```

```output
ANALYZE
```

### Analyze multiple tables verbosely

```plpgsql
yugabyte=# ANALYZE VERBOSE some_table, other_table;
```

```output
INFO:  analyzing "public.some_table"
INFO:  "some_table": scanned, 3 rows in sample, 3 estimated total rows
INFO:  analyzing "public.other_table"
INFO:  "other_table": scanned, 3 rows in sample, 3 estimated total rows
ANALYZE
```

### Analyze affects query plans

This example demonstrates how statistics affect the optimizer.

Let's create a new table...

```sql
yugabyte=# CREATE TABLE test(a int primary key, b int);
```

```output
CREATE TABLE
```

... and populate it.

```sql
yugabyte=# INSERT INTO test VALUES (1, 1), (2, 2), (3, 3);
```

```output
INSERT 0 3
```

In the absence of statistics, the optimizer uses hard-coded defaults, such as 1000 for the number of rows in the table.

```sql
yugabyte=# EXPLAIN select * from test where b = 1;
```

```output
                       QUERY PLAN
---------------------------------------------------------
 Seq Scan on test  (cost=0.00..102.50 rows=1000 width=8)
   Filter: (b = 1)
(2 rows)
```

Now run the ANALYZE command to collect statistics.

```sql
yugabyte=# ANALYZE test;
```

```output
ANALYZE
```

After ANALYZE number of rows is accurate.

```sql
yugabyte=# EXPLAIN select * from test where b = 1;
```

```output
                     QUERY PLAN
----------------------------------------------------
 Seq Scan on test  (cost=0.00..0.31 rows=3 width=8)
   Filter: (b = 1)
(2 rows)
```

Once the optimizer has better idea about data in the tables, it is able to create better performing query plans.

{{< note title="Note" >}}
The query planner currently uses only the number of rows when calculating execution costs of the sequential and index scans.
{{< /note >}}
