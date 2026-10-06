---
title: Table inheritance
headerTitle: Table inheritance
linkTitle: Table inheritance
description: How INHERITS works in YSQL, including columns, constraints, queries, schema changes, and limitations.
menu:
  stable_api:
    identifier: ddl-inherit
    parent: the-sql-language
    weight: 250
type: docs
---

YSQL supports table inheritance with the `INHERITS` clause of [CREATE TABLE](../statements/ddl_create_table/). A child table inherits columns and certain constraints from one or more parent tables. For the allowed differences between a parent column and a child column, and how those differences are resolved, see the [PostgreSQL documentation](https://www.postgresql.org/docs/15/ddl-inherit.html).

{{<lead link="../../../../explore/ysql-language-features/advanced-features/inheritance/">}}
For an accounts schema that uses `INHERITS`, see [Table inheritance example](../../../../explore/ysql-language-features/advanced-features/inheritance/).
{{</lead>}}

## What a child inherits

Columns, column defaults, and check constraints propagate from a parent to its children.

Primary keys, unique constraints, indexes, and foreign keys do not propagate. Define them on each child.

A table can inherit from more than one parent, and a child can itself be a parent. The parent can also contain rows that are not in any child.

## Queries and updates

`SELECT`, `UPDATE`, and `DELETE` on a parent operate on the parent and every child. `ONLY` restricts the statement to the named table. The [example](../../../../explore/ysql-language-features/advanced-features/inheritance/#queries-and-updates-on-data) uses `ONLY` with the accounts tables.

## Schema changes

Adding, dropping, or altering a column on the parent propagates to every child. The result is more complex when a child already has a column of the same name and type, or inherits that column from more than one parent with differing definitions, such as `NULL` versus `NOT NULL`.

Adding or dropping a check constraint propagates to every child. Adding or dropping a primary key, unique constraint, or foreign key does not.

`ONLY` restricts some schema changes to the parent. `ALTER TABLE ONLY accounts DROP COLUMN profit` drops the column on `accounts` and leaves it on the children.

## Limitations

- Dropping or adding a column on a parent fails when a local column of that name exists on a child. {{<issue 26094>}}
- Obtaining a row lock on a parent crashes when a child is a `file_fdw` table. {{<issue 27105>}}

For an up-to-date list, see {{<issue 27949>}}.
