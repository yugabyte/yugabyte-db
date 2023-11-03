---
title: DROP USER statement [YSQL]
headerTitle: DROP USER
linkTitle: DROP USER
description: Use the DROP USER statement to drop a user or role. DROP USER is an alias for DROP ROLE.
menu:
  preview:
    identifier: dcl_drop_user
    parent: statements
aliases:
  - /preview/api/ysql/commands/dcl_drop_user/
type: docs
---

## Synopsis

Use the `DROP USER` statement to drop a user or role. `DROP USER` is an alias for [`DROP ROLE`](../dcl_drop_role) and is used to drop a role.

## Syntax

{{%ebnf%}}
  drop_user
{{%/ebnf%}}

## Semantics

See [`DROP ROLE`](../dcl_drop_role) for more details.

## Example

- Drop a user.

```plpgsql
yugabyte=# DROP USER John;
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`ALTER ROLE`](../dcl_alter_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
