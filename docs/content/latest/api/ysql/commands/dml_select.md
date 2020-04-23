---
title: SELECT statement [YSQL]
headerTitle: SELECT
linkTitle: SELECT
description: Use the SELECT statement to retrieve rows of specified columns that meet a given condition from a table.
menu:
  latest:
    identifier: api-ysql-commands-select
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/dml_select/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `SELECT` statement to retrieve rows of specified columns that meet a given condition from a table. It specifies the columns to be retrieved, the name of the table, and the condition each selected row must satisfy.

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
    {{% includeMarkdown "../syntax_resources/commands/select,order_expr.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/select,order_expr.diagram.md" /%}}
  </div>
</div>

## Semantics

- An error is raised if the specified `table_name` does not exist.
- `*` represents all columns.

While the where clause allows a wide range of operators, the exact conditions used in the where clause have significant performance considerations (especially for large datasets).

### *condition*

Specify an expression that evaluates to a Boolean value.

For details on `from_item`, `grouping_element`, and `with_query` see [SELECT](https://www.postgresql.org/docs/10/static/sql-select.html) in the PostgreSQL documentation.

## Examples

Create two sample tables.

```postgresql
yugabyte=# CREATE TABLE sample1(k1 bigint, k2 float, v text, PRIMARY KEY (k1, k2));
```

```postgresql
yugabyte=# CREATE TABLE sample2(k1 bigint, k2 float, v text, PRIMARY KEY (k1, k2));
```

Insert some rows.

```postgresql
yugabyte=# INSERT INTO sample1(k1, k2, v) VALUES (1, 2.5, 'abc'), (1, 3.5, 'def'), (1, 4.5, 'xyz');
```

```postgresql
yugabyte=# INSERT INTO sample2(k1, k2, v) VALUES (1, 2.5, 'foo'), (1, 4.5, 'bar');
```

Select from both tables using join.

```postgresql
yugabyte=# SELECT a.k1, a.k2, a.v as av, b.v as bv FROM sample1 a LEFT JOIN sample2 b ON (a.k1 = b.k1 and a.k2 = b.k2) WHERE a.k1 = 1 AND a.k2 IN (2.5, 3.5) ORDER BY a.k2 DESC;
```

```
 k1 | k2  | av  | bv
----+-----+-----+-----
  1 | 3.5 | def |
  1 | 2.5 | abc | foo
(2 rows)
```

## See also

- [`CREATE TABLE`](../ddl_create_table)
- [`INSERT`](../dml_insert)
