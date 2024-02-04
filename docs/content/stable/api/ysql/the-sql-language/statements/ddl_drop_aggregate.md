---
title: DROP AGGREGATE statement [YSQL]
headerTitle: DROP AGGREGATE
linkTitle: DROP AGGREGATE
description: Use the DROP AGGREGATE statement to remove an aggregate.
menu:
  stable:
    identifier: ddl_drop_aggregate
    parent: statements
type: docs
---

## Synopsis

Use the `DROP AGGREGATE` statement to remove an aggregate.

## Syntax

{{%ebnf%}}
  drop_aggregate,
  aggregate_signature
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-drop-aggregate].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE AGGREGATE newcnt(*) (
             sfunc = int8inc,
             stype = int8,
             initcond = '0',
             parallel = safe
           );
yugabyte=# DROP AGGREGATE newcnt(*);
```

`IF EXISTS` example.

```plpgsql
yugabyte=# DROP AGGREGATE IF EXISTS newcnt(*);
yugabyte=# CREATE AGGREGATE newcnt(*) (
             sfunc = int8inc,
             stype = int8,
             initcond = '0',
             parallel = safe
           );
yugabyte=# DROP AGGREGATE IF EXISTS newcnt(*);
```

`CASCADE` and `RESTRICT` example.

```plpgsql
yugabyte=# CREATE AGGREGATE newcnt(*) (
             sfunc = int8inc,
             stype = int8,
             initcond = '0',
             parallel = safe
           );
yugabyte=# CREATE VIEW cascade_view AS
             SELECT newcnt(*) FROM pg_aggregate;
yugabyte=# -- The following should error:
yugabyte=# DROP AGGREGATE newcnt(*) RESTRICT;
yugabyte=# -- The following should error:
yugabyte=# DROP AGGREGATE newcnt(*);
yugabyte=# DROP AGGREGATE newcnt(*) CASCADE;
```

## See also

- [`CREATE AGGREGATE`](../ddl_create_aggregate)
- [postgresql-docs-drop-aggregate](https://www.postgresql.org/docs/current/sql-dropaggregate.html)
