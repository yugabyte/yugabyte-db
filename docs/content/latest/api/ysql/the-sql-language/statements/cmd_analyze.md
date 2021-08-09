---
title: ANALYZE statement [YSQL]
headerTitle: ANALYZE
linkTitle: ANALYZE
description: Collect statistics about database tables with the ANALYZE statement.
beta: /latest/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    identifier: cmd_analyze
    parent: statements
aliases:
  - /latest/api/ysql/commands/cmd_analyze/
isTocNested: true
showAsideToc: true
---

## Synopsis

ANALYZE collects statistics about the contents of tables in the database, and stores the results in the `pg_statistic` system catalog. Subsequently, the query planner uses these statistics to help determine the most efficient execution plans for queries.

The Yugabyte implementation is based on the framework provided by PostgreSQL, which requires the storage layer to provide random sample of rows of predefined size. The size is calculated based on number of factors, such as data types of included columns.

{{< note title="Note" >}}
Currently sampling algorithm is not optimized fror a large table. It may take minutes to collect statistics from a table containing several million rows of data.
{{< /note >}}

{{< note title="Note" >}}
Currently Yugabyte doesn't run a background job to analyzing the tables like PostgreSQL's autovacuum.
To collect or update statistics the ANALYZE command should be explicitly executed.
{{< /note >}}

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/analyze,table_and_columns.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/analyze,table_and_columns.diagram.md" /%}}
  </div>
</div>

## Semantics

### VERBOSE

Enable display of progress messages.

### *table_name*

Specify the table, optionally schema-qualified, to be analyzed. If not specified, then all regular tables in the current database will be analyzed.

### *column_name*

Specify the list of columns to be analyzed. If not specified, then all columns of the table will be analyzed.

## Examples

### Analyze single table

```plpgsql
yugabyte=# ANALYZE some_table;
```

```output
ANALYZE
```

### Analyze some columns of the table

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

In the absence of the statistics optimizer uses hard-coded defaults, such us 1000 for the number of rows in the table.
After ANALYZE number of rows is accurate.

```output
yugabyte=# CREATE TABLE test(a int primary key, b int);
CREATE TABLE
yugabyte=# INSERT INTO test VALUES (1, 1), (2, 2), (3, 3);
INSERT 0 3
yugabyte=# EXPLAIN select * from test where b = 1;
                       QUERY PLAN
---------------------------------------------------------
 Seq Scan on test  (cost=0.00..102.50 rows=1000 width=8)
   Filter: (b = 1)
(2 rows)

yugabyte=# ANALYZE test;
ANALYZE
yugabyte=# EXPLAIN select * from test where b = 1;
                     QUERY PLAN
----------------------------------------------------
 Seq Scan on test  (cost=0.00..0.31 rows=3 width=8)
   Filter: (b = 1)
(2 rows)
```

{{< note title="Note" >}}
Yugabyte's query plan nodes (in partiqular sequeential and index scans) were implemented long before we planned to support
the ANALYZE command and statistics, so planner does not make use of statistics besides number of rows when calculating
execution costs of those nodes.
{{< /note >}}
