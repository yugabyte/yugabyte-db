---
title: DROP MATERIALIZED VIEW statement [YSQL]
headerTitle: DROP MATERIALIZED VIEW
linkTitle: DROP MATERIALIZED VIEW
description: Use the DROP MATERIALIZED VIEW statement to drop a materialized view.
menu:
  v2.18:
    identifier: ddl_drop_matview
    parent: statements
type: docs
---

## Synopsis

Use the `DROP MATERIALIZED VIEW` statement to drop a materialized view.

## Syntax

{{%ebnf%}}
  drop_matview
{{%/ebnf%}}

## Semantics

Drop a materialized view named *matview_name*. If `matview_name` already exists in the specified database, an error will be raised unless the `IF NOT EXISTS` clause is used.

### RESTRICT / CASCADE

`RESTRICT` is the default and it will not drop the materialized view if any objects depend on it.

`CASCADE` will drop any objects that transitively depend on the materialized view.

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE TABLE t1(a int4);
yugabyte=# CREATE MATERIALIZED VIEW m1 AS SELECT * FROM t1;
yugabyte=# CREATE MATERIALIZED VIEW m2 AS SELECT * FROM m1;
yugabyte=# DROP MATERIALIZED VIEW m1; -- fails because m2 depends on m1

```

```
ERROR:  cannot drop materialized view m1 because other objects depend on it
DETAIL:  materialized view m2 depends on materialized view m1
```

```plpgsql
yugabyte=# DROP MATERIALIZED VIEW m1 CASCADE; -- succeeds
```

## See also

- [`CREATE MATERIALIZED VIEW`](../ddl_create_matview)
- [`REFRESH MATERIALIZED VIEW`](../ddl_refresh_matview)
