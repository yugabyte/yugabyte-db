---
title: ALTER USER statement [YSQL]
headerTitle: ALTER USER
linkTitle: ALTER USER
description: Use the ALTER USER statement to alter a role. ALTER USER is an alias for ALTER ROLE and is used to alter a role.
menu:
  stable:
    identifier: dcl_alter_user
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER USER` statement to alter a role. `ALTER USER` is an alias for [`ALTER ROLE`](../dcl_alter_role) and is used to alter a role.

## Syntax

{{%ebnf%}}
  alter_user,
  alter_role_option,
  role_specification,
  alter_user_rename,
  alter_user_config,
  config_setting
{{%/ebnf%}}

See [`ALTER ROLE`](../dcl_alter_role) for more details.

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`DROP ROLE`](../dcl_drop_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
