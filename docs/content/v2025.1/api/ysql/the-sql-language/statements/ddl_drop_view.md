---
title: DROP VIEW statement [YSQL]
headerTitle: DROP VIEW
linkTitle: DROP VIEW
description: Use the DROP VIEW statement to drop a view.
menu:
  v2025.1_api:
    identifier: ddl_drop_view
    parent: statements
type: docs
---

## Synopsis

Use the `DROP VIEW` statement to drop a view.

## Syntax

{{%ebnf%}}
  drop_view
{{%/ebnf%}}

## Semantics

Drop a view named *qualified_name*. If `qualified_name` doesn't exist in the specified database, an error will be raised unless the `IF EXISTS` clause is used.

### RESTRICT / CASCADE

`RESTRICT` is the default, and it will not drop the view if any objects depend on it.

`CASCADE` will drop any objects that transitively depend on the view.

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE TABLE t1(a int4);
yugabyte=# CREATE VIEW m1 AS SELECT * FROM t1;
yugabyte=# CREATE VIEW m2 AS SELECT * FROM m1;
yugabyte=# DROP VIEW m1; -- fails because m2 depends on m1

```

```
ERROR:  cannot drop view m1 because other objects depend on it
DETAIL:  view m2 depends on view m1
HINT:  Use DROP ... CASCADE to drop the dependent objects too.
```

```plpgsql
yugabyte=# DROP VIEW m1 CASCADE; -- succeeds
```

## See also

- [`CREATE VIEW`](../ddl_create_view)
- [`DROP MATERIALIZED VIEW`](../ddl_drop_matview/)
