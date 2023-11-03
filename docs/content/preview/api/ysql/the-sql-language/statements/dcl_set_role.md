---
title: SET ROLE statement [YSQL]
headerTitle: SET ROLE
linkTitle: SET ROLE
description: Use the SET ROLE statement to set the current user of the current session to be the specified user.
menu:
  preview:
    identifier: dcl_set_role
    parent: statements
aliases:
  - /preview/api/ysql/commands/dcl_set_role/
type: docs
---

## Synopsis

Use the `SET ROLE` statement to set the current user of the current session to be the specified user.

## Syntax

{{%ebnf%}}
  set_role,
  reset_role
{{%/ebnf%}}

## Semantics

The specified `role_name` must be a role that the current session user is a member of. Superusers can set to any role.
Once the role is set to `role_name`, any further SQL commands will use the privileges available to that role.

To reset the role back to current user, `RESET ROLE` or `SET ROLE NONE` can be used.

## Examples

- Change to new role John.

```plpgsql
yugabyte=# select session_user, current_user;
 session_user | current_user
--------------+--------------
 yugabyte     | yugabyte
(1 row)
yugabyte=# set role john;
SET
yugabyte=# select session_user, current_user;
 session_user | current_user
--------------+--------------
 yugabyte     | john
(1 row)
```

- Changing to new role assumes the privileges available to that role.

```plpgsql
yugabyte=# select session_user, current_user;
 session_user | current_user
--------------+--------------
 yugabyte     | yugabyte
(1 row)
yugabyte=# create database db1;
CREATE DATABASE
yugabyte=# set role john;
SET
yugabyte=# select session_user, current_user;
 session_user | current_user
--------------+--------------
 yugabyte     | john
(1 row)
yugabyte=# create database db2;
ERROR:  permission denied to create database
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
