---
title: GRANT statement [YSQL]
headerTitle: GRANT
linkTitle: GRANT
description: Use the GRANT statement to grant access privileges on database objects as well as to assign membership in roles.
menu:
  stable_api:
    identifier: dcl_grant
    parent: statements
type: docs
---

## Synopsis

Use the GRANT statement to grant access privileges on database objects as well as to assign membership in roles.

## Syntax

{{%ebnf%}}
  grant,
  grant_table,
  grant_table_col,
  grant_seq,
  grant_db,
  grant_domain,
  grant_schema,
  grant_type,
  grant_role,
  grantee_role
{{%/ebnf%}}

## Semantics

GRANT can be used to assign privileges on database objects as well as memberships in roles.

### GRANT on database objects

This variant of GRANT command is used to assign privileges on database objects to one or more roles.
If keyword PUBLIC is used instead of `role_name`, then it means that the privileges are to be granted to all roles, including those that might be created later.

If WITH GRANT OPTION is specified, the recipient of the privilege can in turn grant it to others. Without a grant option, the recipient cannot do that. Grant options cannot be granted to PUBLIC.

There is no need to grant privileges to the owner of an object (usually the user that created it), as the owner has all privileges by default. (The owner could, however, choose to revoke some of their own privileges for safety.)

Possible privileges are

- SELECT

  - This allows SELECT from any or specified columns of the specified table, view, or sequence. It also allows the use of COPY TO. This privilege is also needed to reference column values in UPDATE or DELETE.

- INSERT

  - This allows INSERT of a row into the specified table. If specific columns are listed, only those columns may be assigned to in the INSERT command (other columns will therefore receive default values). Also allows COPY FROM.

- UPDATE

  - This allows UPDATE of any column, or the specific columns listed, of the specified table.

- DELETE
  - This allows DELETE of a row from the specified table.

- TRUNCATE

  - This allows TRUNCATE on the specified table.

- REFERENCES

  - This allows creation of a foreign key constraint referencing the specified table, or specified columns of the table.

- TRIGGER

  - This allows the creation of a trigger on the specified table.

- CREATE

  - For databases, this allows schemas to be created within the database.
  - For schemas, this allows objects to be created within the schema. To rename an object, you must own the object and have this privilege for the containing schema.

- CONNECT

  - This allows the user to connect to the specified database. This privilege is checked at connection startup.

- TEMPORARY / TEMP

  - This allows temporary tables to be created while using the specified database.

- EXECUTE

  - Allows the use of the specified function or procedure and the use of any operators that are implemented on top of the function.

- USAGE

  - For schemas, this allows access to objects contained in the specified schema (assuming that the objects' own privilege requirements are also met). Essentially this allows the grantee to "look up" objects within the schema.
  - For sequences, this privilege allows the use of the `currval()` and `nextval()` functions.
  - For types and domains, this privilege allows the use of the type or domain in the creation of tables, functions, and other schema objects.

- ALL PRIVILEGES

  - Grant all privileges at once.

## Predefined roles

YugabyteDB ships with built-in roles that grant access to frequently required administrative functions and data. Database administrators (and anyone with the CREATEROLE attribute) can assign these predefined roles to users or other roles, thereby giving them the necessary permissions to perform specific tasks and access certain information.

Some of the predefined roles are as follows.

Role  | Access | Info
----- | -------| ----
pg_read_all_data | Grants read-only access to all tables, views, and sequences in all schemas of a database. | Ideal for users or applications needing to query data without modifying it.
pg_write_all_data | Grants write access to all tables in a database, including privileges for INSERT, UPDATE, DELETE, and TRUNCATE. | Suitable for applications or users that need to modify data but not manage schema objects.
pg_read_all_settings | Allows users to view all database configuration settings. | Useful for monitoring and diagnostics without granting administrative privileges.
pg_read_all_stats | Grants access to view all statistical information in system catalogs, such as pg_stat_* views. | Useful for performance monitoring and query analysis.
pg_stat_scan_tables | Grants permission to execute pg_stat_reset() on individual tables, resetting their statistics. | Helpful for users managing table-specific performance metrics.
pg_monitor | Combines privileges from pg_read_all_settings and pg_read_all_stats. | Allows access to monitor the database while restricting modification or administrative tasks.
pg_signal_backend | Enables sending signals to backend processes, such as using pg_terminate_backend() or pg_cancel_backend(). | Useful for terminating queries or sessions.
pg_database_owner | Grants privileges that apply to the owner of the database. | Used for tasks specific to database management without granting superuser access.

To provide read access to all objects in a database to user `alice`, an administrator would need to run the following:

```sql
GRANT pg_read_all_data TO alice;
```

### GRANT on roles

This variant of GRANT is used to grant membership in a role to one or more other roles.
If WITH ADMIN OPTION is specified, the member can in turn grant membership in the role to others, and revoke membership in the role as well.

## Examples

- Grant SELECT privilege to all users on table `stores`.

  ```plpgsql
  yugabyte=# GRANT SELECT ON stores TO PUBLIC;
  ```

- Add user John to `SysAdmins` group.

  ```plpgsql
  yugabyte=# GRANT SysAdmins TO John;
  ```

## See also

- [REVOKE](../dcl_revoke)
- [CREATE ROLE](../dcl_create_role)
