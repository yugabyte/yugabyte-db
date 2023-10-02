---
title: CREATE MATERIALIZED VIEW statement [YSQL]
headerTitle: CREATE MATERIALIZED VIEW
linkTitle: CREATE MATERIALIZED VIEW
description: Use the CREATE MATERIALIZED VIEW statement to create a materialized view.
menu:
  preview:
    identifier: ddl_create_matview
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_create_matview/
type: docs
---

## Synopsis

Use the `CREATE MATERIALIZED VIEW` statement to create a materialized view.

## Syntax

{{%ebnf%}}
  create_matview
{{%/ebnf%}}

## Semantics

Create a materialized view named *matview_name*. If `matview_name` already exists in the specified database, an error will be raised unless the `IF NOT EXISTS` clause is used.

### Tablespace

Used to specify the tablespace for the materialized view.

### Storage parameters

COLOCATION

Specify `COLOCATION = true` for the materialized view to be colocated. The default value of this option is false.

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE TABLE t1(a int4, b int4);
yugabyte=# INSERT INTO t1 VALUES (2, 4), (3, 4);
yugabyte=# CREATE MATERIALIZED VIEW m1 AS SELECT * FROM t1 WHERE a = 3;
yugabyte=# SELECT * FROM t1;
```

```output
 a | b
---+---
 3 | 4
 2 | 4
(2 rows)
```

```plpgsql
yugabyte=# SELECT * FROM m1;
```

```output
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
