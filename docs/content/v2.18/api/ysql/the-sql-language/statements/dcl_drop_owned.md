---
title: DROP OWNED statement [YSQL]
headerTitle: DROP OWNED
linkTitle: DROP OWNED
description: Use the DROP OWNED statement to drop all database objects within the current database that are owned by one of the specified roles.
menu:
  v2.18:
    identifier: dcl_drop_owned
    parent: statements
type: docs
---

## Synopsis

Use the `DROP OWNED` statement to drop all database objects within the current database that are owned by one of the specified roles.
Any privileges granted to the given roles on objects in the current database or on shared objects will also be revoked.

## Syntax

{{%ebnf%}}
  drop_owned,
  role_specification
{{%/ebnf%}}

## Semantics

- CASCADE

Automatically drop objects that depend on the affected objects.

- RESTRICT

This is the default mode and will raise an error if there are other database objects that depend on the dropped objects.

## Examples

- Drop all objects owned by `john`.

```plpgsql
yugabyte=# drop owned by john;
```

## See also

- [`REASSIGN OWNED`](../dcl_reassign_owned)
- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
