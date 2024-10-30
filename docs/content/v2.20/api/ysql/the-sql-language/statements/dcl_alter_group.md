---
title: ALTER GROUP statement [YSQL]
headerTitle: ALTER GROUP
linkTitle: ALTER GROUP
description: Use the `ALTER GROUP` statement to alter attributes for a group (role).
menu:
  v2.20:
    identifier: dcl_alter_group
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER GROUP` statement to alter attributes for a group (role).
This is added for compatibility with Postgres. Its usage is discouraged. [`ALTER ROLE`](../dcl_alter_role) is the preferred way to change attributes of a role.

## Syntax

{{%ebnf%}}
  alter_group,
  role_specification,
  alter_group_rename
{{%/ebnf%}}


See [`ALTER ROLE`](../dcl_alter_role) for more details.

`ALTER GROUP` can be used to add or remove roles from a group. Please use [`GRANT`](../dcl_grant) or [`REVOKE`](../dcl_revoke) instead.
It can also be used to rename a role.

## See also

- [`ALTER ROLE`](../dcl_alter_role)
- [`CREATE ROLE`](../dcl_create_role)
- [`DROP ROLE`](../dcl_drop_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
