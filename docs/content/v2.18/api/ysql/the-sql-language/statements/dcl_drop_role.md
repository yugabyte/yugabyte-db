---
title: DROP ROLE statement [YSQL]
headerTitle: DROP ROLE
linkTitle: DROP ROLE
description: Use the DROP ROLE statement to remove the specified roles.
menu:
  v2.18:
    identifier: dcl_drop_role
    parent: statements
type: docs
---

## Synopsis

Use the `DROP ROLE` statement to remove the specified roles.

## Syntax

{{%ebnf%}}
  drop_role
{{%/ebnf%}}

Where

- `role_name` is the name of the role to be removed.

To drop a superuser role, you must be a superuser yourself. To drop non-superuser roles, you must have CREATEROLE privilege.

Before dropping the role, you must drop all the objects it owns (or reassign their ownership) and revoke any privileges the role has been granted on other objects. The `REASSIGN OWNED` and `DROP OWNED` commands can be used for this purpose.

It is, however, not necessary to remove role memberships involving the role. `DROP ROLE` automatically revokes any memberships of the target role in other roles, and of other roles in the target role. The other roles are not dropped or affected.

## Example

- Drop a role.

```plpgsql
yugabyte=# DROP ROLE John;
```

## See also

- [`ALTER ROLE`](../dcl_alter_role)
- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
