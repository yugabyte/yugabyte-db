---
title: CREATE USER statement [YSQL]
headerTitle: CREATE USER
linkTitle: CREATE USER
description: Use the CREATE USER statement to create a user. The CREATE USER statement is an alias for CREATE ROLE, but creates a role that has LOGIN privileges by default.
menu:
  v2.18:
    identifier: dcl_create_user
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE USER` statement to create a user. The `CREATE USER` statement is an alias for [`CREATE ROLE`](../dcl_create_role), but creates a role that has LOGIN privileges by default.

## Syntax

{{%ebnf%}}
  create_user,
  role_option
{{%/ebnf%}}

## Semantics

See [`CREATE ROLE`](../dcl_create_role) for more details.

## Examples

- Create a sample user with password.

```plpgsql
yugabyte=# CREATE USER John WITH PASSWORD 'password';
```

- Grant John all permissions on the `yugabyte` database.

```plpgsql
yugabyte=# GRANT ALL ON DATABASE yugabyte TO John;
```

- Remove John's permissions from the `yugabyte` database.

```plpgsql
yugabyte=# REVOKE ALL ON DATABASE yugabyte FROM John;
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
