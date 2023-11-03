---
title: REFRESH MATERIALIZED VIEW statement [YSQL]
headerTitle: REFRESH MATERIALIZED VIEW
linkTitle: REFRESH MATERIALIZED VIEW
description: Use the REFRESH MATERIALIZED VIEW statement to refresh the contents of a materialized view.
menu:
  preview:
    identifier: ddl_refresh_matview
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_refresh_matview/
type: docs
---

## Synopsis

Use the `REFRESH MATERIALIZED VIEW` statement to replace the contents of a materialized view.

## Syntax

{{%ebnf%}}
  refresh_matview
{{%/ebnf%}}

## Semantics

Replace the contents of a materialized view named *matview_name*.

### WITH DATA
If `WITH DATA` (default) is specified, the view's query is executed to obtain the new data and the materialized view's contents are updated.

### WITH NO DATA
If `WITH NO DATA` is specified, the old contents of the materialized view are discarded and the materialized view is left in an unscannable state.

### CONCURRENTLY
This option may be faster in the cases where a small number of rows require updates. Without this option, a refresh that updates a large number of rows will tend to use fewer resources and complete quicker.
This option is only permitted when there is at least one UNIQUE index on the materialized view, and when the materialized view is populated.
`CONCURRENTLY` AND `WITH NO DATA` cannot be used together. Moreover, the `CONCURRENTLY` option can also not be used when there are rows where _all_ the columns are null. In both scenarios, refreshing the materialized view will raise an error.

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
## Limitations

- Materialized views must be refreshed manually using the `REFRESH` command. Automatic refreshes are not supported.
- It is currently not possible to refresh materialized views inside an explicit transaction. (See <https://github.com/yugabyte/yugabyte-db/issues/1404>)


## See also

- [`CREATE MATERIALIZED VIEW`](../ddl_create_matview)
- [`DROP MATERIALIZED VIEW`](../ddl_drop_matview)
