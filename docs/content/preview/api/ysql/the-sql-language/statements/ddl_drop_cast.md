---
title: DROP CAST statement [YSQL]
headerTitle: DROP CAST
linkTitle: DROP CAST
description: Use the DROP CAST statement to remove a cast.
menu:
  preview:
    identifier: ddl_drop_cast
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_drop_cast/
type: docs
---

## Synopsis

Use the `DROP CAST` statement to remove a cast.

## Syntax

{{%ebnf%}}
  drop_cast
{{%/ebnf%}}

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-drop-cast].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE FUNCTION sql_to_date(integer) RETURNS date AS $$
             SELECT $1::text::date
             $$ LANGUAGE SQL IMMUTABLE STRICT;
yugabyte=# CREATE CAST (integer AS date) WITH FUNCTION sql_to_date(integer) AS ASSIGNMENT;
yugabyte=# DROP CAST (integer AS date);
```

## See also

- [`CREATE CAST`](../ddl_create_cast)
- [postgresql-docs-drop-cast](https://www.postgresql.org/docs/current/sql-dropcast.html)
