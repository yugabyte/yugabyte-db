---
title: CREATE MATERIALIZED VIEW statement [YSQL]
headerTitle: CREATE MATERIALIZED VIEW
linkTitle: CREATE MATERIALIZED VIEW
description: Use the CREATE MATERIALIZED VIEW statement to create a materialized view.
menu:
  v2.12:
    identifier: ddl_create_matview
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE MATERIALIZED VIEW` statement to create a materialized view.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_matview.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_matview.diagram.md" %}}
  </div>
</div>

## Semantics

Create a materialized view named *matview_name*. If `matview_name` already exists in the specified database, an error will be raised unless the `IF NOT EXISTS` clause is used.

### Tablespace
Used to specify the tablespace for the materialized view.

### Storage parameters

COLOCATED

Specify `COLOCATED = true` for the materialized view to be colocated. The default value of this option is false.

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE TABLE t1(a int4, b int4);
yugabyte=# INSERT INTO t1 VALUES (2, 4), (3, 4);
yugabyte=# CREATE MATERIALIZED VIEW m1 AS SELECT * FROM t1 WHERE a = 3;
yugabyte=# SELECT * FROM t1;
```

```
 a | b
---+---
 3 | 4
 2 | 4
(2 rows)
```

```plpgsql
yugabyte=# SELECT * FROM m1;
```

```
 a | b
---+---
 3 | 4
(1 row)
```

## Limitations

- Materialized views are not supported in YCQL

## See also

- [`REFRESH MATERIALIZED VIEW`](../ddl_refresh_matview)
- [`DROP MATERIALIZED VIEW`](../ddl_drop_matview)
