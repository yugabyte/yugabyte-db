---
title: SET SESSION AUTHORIZATION statement [YSQL]
headerTitle: SET SESSION AUTHORIZATION
linkTitle: SET SESSION AUTHORIZATION
description: Use the SET SESSION AUTHORIZATION statement to set the current user and session user of the current session to be the specified user.
menu:
  v2.18:
    identifier: dcl_set_session_authorization
    parent: statements
type: docs
---

## Synopsis

Use the `SET SESSION AUTHORIZATION` statement to set the current user and session user of the current session to be the specified user.

## Syntax

{{%ebnf%}}
  set_session_authorization,
  reset_session_authorization
{{%/ebnf%}}

## Semantics

Session user can only be changed by superusers.
Once the session user is set to `role_name`, any further SQL commands will use the privileges available to that role.

To reset the session user back to current authenticated user, `RESET SESSION AUTHORIZATION` or `SET SESSION AUTHORIZATION DEFAULT` can be used.

## Examples

- Set session user to John.

```plpgsql
yugabyte=# select session_user, current_user;
 session_user | current_user
--------------+--------------
 yugabyte     | yugabyte
(1 row)
yugabyte=# set session authorization john;
SET
yugabyte=# select session_user, current_user;
 session_user | current_user
--------------+--------------
 john     | john
(1 row)
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
