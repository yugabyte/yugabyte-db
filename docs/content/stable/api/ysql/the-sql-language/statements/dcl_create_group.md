---
title: CREATE GROUP statement [YSQL]
headerTitle: CREATE GROUP
linkTitle: CREATE GROUP
description: Use the CREATE GROUP statement to create a group role. CREATE GROUP is an alias for CREATE ROLE and is used to create a group role.
menu:
  stable:
    identifier: dcl_create_group
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE GROUP` statement to create a group role. `CREATE GROUP` is an alias for [`CREATE ROLE`](../dcl_create_role) and is used to create a group role.

## Syntax

{{%ebnf%}}
  create_group,
  role_option
{{%/ebnf%}}


See [`CREATE ROLE`](../dcl_create_role) for more details.

## Examples

- Create a sample group that can manage databases and roles.

```plpgsql
yugabyte=# CREATE GROUP SysAdmin WITH CREATEDB CREATEROLE;
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
