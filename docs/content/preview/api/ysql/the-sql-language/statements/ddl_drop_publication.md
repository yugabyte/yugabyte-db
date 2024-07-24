---
title: DROP PUBLICATION statement [YSQL]
headerTitle: DROP PUBLICATION
linkTitle: DROP PUBLICATION
description: Use the DROP PUBLICATION statement to remove a publication from a database.
menu:
  preview:
    identifier: ddl_drop_publication
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_drop_publication/
type: docs
---

## Synopsis

Use the `DROP PUBLICATION` statement to remove a publication from a database.

## Syntax

{{%ebnf%}}
  drop_publication,
{{%/ebnf%}}

## Semantics

Drop a publication named *publication_name*. If `publication_name` doesn't already exists in the specified database, an error will be raised unless the `IF EXISTS` clause is used.

### RESTRICT / CASCADE

These key words do not have any effect, since there are no dependencies on publications.

### Permissions

A publication can only be dropped by its owner or a superuser.

## Examples

Basic example.

```sql
yugabyte=# DROP PUBLICATION mypublication;
```

## See also

- [`CREATE PUBLICATION`](../ddl_create_publication)
- [`ALTER PUBLICATION`](../ddl_alter_publication)
