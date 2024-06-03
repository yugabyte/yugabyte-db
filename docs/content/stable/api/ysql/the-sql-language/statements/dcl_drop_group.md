---
title: DROP GROUP statement [YSQL]
linkTitle: DROP GROUP
description: Use the DROP GROUP statement to drop a role. DROP GROUP is an alias for DROP ROLE and is used to drop a role.
menu:
  stable:
    identifier: dcl_drop_group
    parent: statements
type: docs
---

## Synopsis

Use the `DROP GROUP` statement to drop a role. `DROP GROUP` is an alias for [`DROP ROLE`](../dcl_drop_role) and is used to drop a role.

## Syntax

{{%ebnf%}}
  drop_group
{{%/ebnf%}}

## Semantics

See [`DROP ROLE`](../dcl_drop_role) for more details.

## Example

- Drop a group.

```plpgsql
yugabyte=# DROP GROUP SysAdmin;
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`ALTER ROLE`](../dcl_alter_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
