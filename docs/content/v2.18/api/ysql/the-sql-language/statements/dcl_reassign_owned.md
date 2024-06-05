---
title: REASSIGN OWNED statement [YSQL]
headerTitle: REASSIGN OWNED
linkTitle: REASSIGN OWNED
description: Use the REASSIGN OWNED statement to change the ownership of database objects owned by any of the "old_roles" to "new_role".
menu:
  v2.18:
    identifier: dcl_reassign_owned
    parent: statements
type: docs
---

## Synopsis

Use the `REASSIGN OWNED` statement to change the ownership of database objects owned by any of the `old_roles` to `new_role`.

## Syntax

{{%ebnf%}}
  reassign_owned,
  role_specification
{{%/ebnf%}}

## Semantics

`REASSIGN OWNED` is typically used to prepare for the removal of a role. It requires membership on both the source roles and target role.

## Examples

- Reassign all objects owned by john to yugabyte.

```plpgsql
yugabyte=# reassign owned by john to yugabyte;
```

## See also

- [`DROP OWNED`](../dcl_drop_owned)
- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
