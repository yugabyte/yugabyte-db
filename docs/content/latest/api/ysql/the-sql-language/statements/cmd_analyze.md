---
title: ANALYZE statement [YSQL]
headerTitle: ANALYZE
linkTitle: ANALYZE
description: Collect statistics about database tables with the ANALYZE statement.
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

ANALYZE collects statistics about the contents of tables in the database, and stores the results in the pg_statistic system catalog. Subsequently, the query planner uses these statistics to help determine the most efficient execution plans for queries.

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
ANALYZE
```

### Analyze some colums of the table

```plpgsql
yugabyte=# ANALYZE some_table(col1, col3);
ANALYZE
```

### Analyze multiple tables verbosely

```plpgsql
yugabyte=# ANALYZE VERBOSE some_table, other_table;
INFO:  analyzing "public.some_table"
INFO:  "some_table": scanned, 3 rows in sample, 3 estimated total rows
INFO:  analyzing "public.other_table"
INFO:  "other_table": scanned, 3 rows in sample, 3 estimated total rows
ANALYZE
```
