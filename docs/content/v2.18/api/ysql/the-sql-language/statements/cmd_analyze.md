---
title: ANALYZE statement [YSQL]
headerTitle: ANALYZE
linkTitle: ANALYZE
description: Collect statistics about database tables with the ANALYZE statement.
techPreview: /preview/releases/versioning/#feature-availability
menu:
  v2.18:
    identifier: cmd_analyze
    parent: statements
type: docs
---

## Synopsis

ANALYZE collects statistics about the contents of tables in the database, and stores the results in the `pg_statistic` system catalog. These statistics help the query planner to determine the most efficient execution plans for queries.

The YugabyteDB implementation is based on the framework provided by PostgreSQL, which requires the storage layer to provide a random sample of rows of a predefined size. The size is calculated based on a number of factors, such as the included columns' data types.

{{< note title="Note" >}}
The sampling algorithm is not currently optimized for large tables. It may take several minutes to collect statistics from a table containing millions of rows of data.
{{< /note >}}

{{< note title="Note" >}}
Currently, YugabyteDB doesn't run a background job like PostgreSQL's autovacuum to analyze the tables.
To collect or update statistics, run the ANALYZE command manually.
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
