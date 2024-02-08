---
title: ALTER DEFAULT PRIVILEGES statement [YSQL]
headerTitle: ALTER DEFAULT PRIVILEGES
linkTitle: ALTER DEFAULT PRIVILEGES
description: Use the ALTER DEFAULT PRIVILEGES statement to define the default access privileges.
menu:
  v2.18:
    identifier: dcl_alter_default_privileges
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER DEFAULT PRIVILEGES` statement to define the default access privileges.

## Syntax

{{%ebnf%}}
  alter_default_priv,
  abbr_grant_or_revoke,
  a_grant_table,
  a_grant_seq,
  a_grant_func,
  a_grant_type,
  a_grant_schema,
  a_revoke_table,
  a_revoke_seq,
  a_revoke_func,
  a_revoke_type,
  a_revoke_schema,
  grant_table_priv,
  grant_seq_priv,
  grantee_role
{{%/ebnf%}}

## Semantics

`ALTER DEFAULT PRIVILEGES` defines the privileges for objects created in future. It does not affect objects that are already created.

Users can change default privileges only for objects that are created by them or by roles that they are a member of.

## Examples

- Grant SELECT privilege to all tables that are created in schema marketing to all users.

  ```plpgsql
  yugabyte=# ALTER DEFAULT PRIVILEGES IN SCHEMA marketing GRANT SELECT ON TABLES TO PUBLIC;
  ```

- Revoke INSERT privilege on all tables from user john.

  ```plpgsql
  yugabyte=# ALTER DEFAULT PRIVILEGES REVOKE INSERT ON TABLES FROM john;
  ```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
