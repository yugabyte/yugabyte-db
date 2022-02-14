---
title: REFRESH MATERIALIZED VIEW statement [YSQL]
headerTitle: REFRESH MATERIALIZED VIEW
linkTitle: REFRESH MATERIALIZED VIEW
description: Use the REFRESH MATERIALIZED VIEW statement to refresh the contents of a materialized view.
menu:
  latest:
    identifier: refresh_matview
    parent: statements
aliases:
  - /latest/api/ysql/commands/refresh_matview/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `REFRESH MATERIALIZED VIEW` statement to replace the contents of a materialized view.

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
    {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/refresh_matview.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/refresh_matview.diagram.md" /%}}
  </div>
</div>

## Semantics

Replace the contents of a materialized view named *matview_name*. 

### WITH DATA
If `WITH DATA` (default) is specified, the view's query is executed to obtain the new data and the materialized view's contents are updated.

### WITH NO DATA
If `WITH NO DATA` is specified, the old contents of the materialized view are discarded and the materialized view is left in an unscannable state.

### CONCURRENTLY
Used to refresh the materialized view without locking out concurrent selects on the materialized view. 
This option is only permitted when there is at least one UNIQUE index on the materialized view, and when the materialized view is populated.
`CONCURRENTLY` AND `WITH NO DATA` may not be used together.

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE TABLE t1(a int4);
yugabyte=# CREATE MATERIALIZED VIEW m1 AS SELECT * FROM t1;
yugabyte=# INSERT INTO t1 VALUES (1);
yugabyte=# SELECT * FROM m1;
```

```
 a
---
(0 rows)
```

```plpgsql
yugabyte=# REFRESH MATERIALIZED VIEW m1;
yugabyte=# SELECT * FROM m1;
```

```
 a
---
 1
(1 row)
```

## See also

- [`CREATE MATERIALIZED VIEW`](../ddl_create_matview)
- [`DROP MATERIALIZED VIEW`](../ddl_drop_matview)
