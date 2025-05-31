---
title: CREATE USER statement [YSQL]
headerTitle: CREATE USER
linkTitle: CREATE USER
description: Use the CREATE USER statement to create a user. The CREATE USER statement is an alias for CREATE ROLE, but creates a role that has LOGIN privileges by default.
menu:
  v2025.1_api:
    identifier: dcl_create_user
    parent: statements
type: docs
---

## Synopsis

Use the CREATE USER statement to create a user. The CREATE USER statement is an alias for [CREATE ROLE](../dcl_create_role), but creates a role that has LOGIN privileges by default.

## Syntax

{{%ebnf%}}
  create_user,
  role_option
{{%/ebnf%}}

## Semantics

See [CREATE ROLE](../dcl_create_role) for more details.

## Public schema privileges

In releases prior to {{<release "2.25">}}, which were PostgreSQL 11 compatible, new database users were automatically granted CREATE and USAGE privileges on the public schema. This allowed them to create objects, such as tables and views, in the shared schema by default.

Starting with release {{<release "2.25">}} (PostgreSQL 15 compatible), this default privilege is no longer provided. New users can no longer create objects in the public schema unless explicitly authorized. Administrators must now manually grant CREATE or other required privileges on the public schema to specific roles or users as needed. This provides a more secure default configuration and protection against attacks described in {{<cve "CVE-2018-1058">}}.

Administrators can grant access to the public schema to specific users as follows:

```sql
GRANT CREATE ON SCHEMA public TO <username>;
```

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

- Create a user with a password that expires in 4 hours.

  ```plpgsql
  yugabyte=# DO $$
  DECLARE time TIMESTAMP := now() + INTERVAL '4 HOURS';
  BEGIN
    EXECUTE format(
      'CREATE USER Edwin WITH PASSWORD ''secure_password'' VALID UNTIL ''%s'';',
      time
    );
  END
  $$;
  ```

## See also

- [CREATE ROLE](../dcl_create_role)
- [GRANT](../dcl_grant)
- [REVOKE](../dcl_revoke)
