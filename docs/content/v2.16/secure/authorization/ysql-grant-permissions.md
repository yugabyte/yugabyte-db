---
title: Grant YSQL privileges in YugabyteDB
headerTitle: Grant privileges
linkTitle: Grant privileges
description: Grant YSQL privileges in YugabyteDB
menu:
  v2.16:
    name: Grant privileges
    identifier: ysql-grant-permissions
    parent: authorization
    weight: 735
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql-grant-permissions/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../ycql-grant-permissions/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

This tutorial demonstrates how to grant privileges in YSQL using the scenario of a company with an engineering organization that has three sub-teams: developers, QA, and DB admins.

Here is what you want to achieve from a role-based access control (RBAC) perspective:

- All members of engineering should be able to read data from any database and table.
- Both developers and QA should be able to modify data in existing tables in the database `dev_database`.
- QA should be able to alter the `integration_tests` table in the database `dev_database`.
- DB admins should be able to perform all operations on any database.

The exercise assumes you have [enabled authentication for YSQL](../../enable-authentication/ysql/).

## 1. Create role hierarchy

Connect to the cluster using a superuser role. For this tutorial, use the default `yugabyte` user and connect to the cluster using `ysqlsh` as follows:

```sh
$ ./bin/ysqlsh
```

Create a database `dev_database`.

```sql
yugabyte=# CREATE database dev_database;
```

Switch to the `dev_database`.

```sql
yugabyte=# \c dev_database
```

Create the `integration_tests` table:

```sql
dev_database=# CREATE TABLE integration_tests (
                 id UUID PRIMARY KEY,
                 time TIMESTAMP,
                 result BOOLEAN,
                 details JSONB
                 );
```

Next, create roles `engineering`, `developer`, `qa`, and `db_admin`.

```sql
dev_database=# CREATE ROLE engineering;
                 CREATE ROLE developer;
                 CREATE ROLE qa;
                 CREATE ROLE db_admin;
```

Grant the `engineering` role to `developer`, `qa`, and `db_admin` roles, as they are all a part of the engineering organization.

```sql
dev_database=# GRANT engineering TO developer;
                 GRANT engineering TO qa;
                 GRANT engineering TO db_admin;
```

## 2. List privileges for roles

You can list all privileges granted to the various roles with the [\du meta-command](../../../admin/ysqlsh/#du-s-pattern-patterns):

```sql
dev_database=# \du
```

You should see something like the following output.

```output
                                      List of roles
  Role name   |                         Attributes                         |   Member of
--------------+------------------------------------------------------------+---------------
 db_admin     | Cannot login                                               | {engineering}
 developer    | Cannot login                                               | {engineering}
 engineering  | Cannot login                                               | {}
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 qa           | Cannot login                                               | {engineering}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

This shows the various role attributes of the `yugabyte` role. Because `yugabyte` is a superuser, it has all privileges on all databases.

## 3. Grant privileges to roles

In this section, you grant permissions to each role.

### Grant read access

All members of engineering should be able to read data from any database and table. Use the `GRANT` statement to grant `SELECT` (or read) access on the existing table (`integration_tests`) to the `engineering` role. This can be done as follows:

```sql
dev_database=# GRANT SELECT ON integration_tests to engineering;
dev_database=# GRANT USAGE ON SCHEMA public TO engineering;
```

Verify that the `engineering` role has `SELECT` privilege on the table using the [\z meta-command](../../../admin/ysqlsh/#z-pattern-patterns), which lists tables with their associated access privileges:

```sql
dev_database=# \z
```

The output should look similar to below.

```output
 Schema |       Name        | Type  |     Access privileges     | Column privileges | Policies
--------+-------------------+-------+---------------------------+-------------------+----------
 public | integration_tests | table | yugabyte=arwdDxt/yugabyte+|                   |
        |                   |       | engineering=r/yugabyte   +|                   |
```

The access privileges "arwdDxt" include all privileges for the user `yugabyte` (superuser), while the role `engineering` has only "r" (read) privileges. For details on the `GRANT` statement and access privileges, see [GRANT](../../../api/ysql/the-sql-language/statements/dcl_grant).

Granting the role `engineering` to any other role causes all those roles to inherit the specified privileges. Thus, `developer`, `qa`, and `db_admin` all inherit the `SELECT` and `USAGE` privileges, giving them read-access.

### Grant data modify access

Both `developers` and `qa` should be able to modify data existing tables in the database `dev_database`. They should be able to execute statements such as `INSERT`, `UPDATE`, `DELETE` or `TRUNCATE` to modify data on existing tables. This can be done as follows:

```sql
dev_database=# GRANT INSERT, UPDATE, DELETE, TRUNCATE ON table integration_tests TO developer;
dev_database=# GRANT INSERT, UPDATE, DELETE, TRUNCATE ON table integration_tests TO qa;
```

Verify that the `developer` and `qa` roles have the appropriate privileges.

```sql
dev_database=# \z
```

```output
                                       Access privileges
 Schema |       Name        | Type  |     Access privileges     | Column privileges | Policies
--------+-------------------+-------+---------------------------+-------------------+----------
 public | integration_tests | table | yugabyte=arwdDxt/yugabyte+|                   |
        |                   |       | engineering=r/yugabyte   +|                   |
        |                   |       | developer=awdD/yugabyte  +|                   |
        |                   |       | qa=awdD/yugabyte          |                   |
```

Now `developer` and `qa` roles have the access privileges `awdD` (append/insert, write/update, delete, and truncate) for the table `integration_tests`.

### Grant alter table access

QA (`qa`) should be able to alter the table `integration_tests` in the database `dev_database`. This can be done as follows.

```sql
dev_database=# ALTER TABLE integration_tests OWNER TO qa;
```

Run the following command to verify the privileges.

```sql
dev_database=# \z
```

Owner has changed from `yugabyte` to `qa` and `qa` has all access privileges (`arwdDxt`) on the table `integration_tests`.

```output
                                   Access privileges
 Schema |       Name        | Type  | Access privileges | Column privileges | Policies
--------+-------------------+-------+-------------------+-------------------+----------
 public | integration_tests | table | qa=arwdDxt/qa    +|                   |
        |                   |       | engineering=r/qa +|                   |
        |                   |       | developer=awdD/qa |                   |
```

### Grant all privileges

DB admins should be able to perform all operations on the database. You can do this by granting DB admins the superuser privilege. Doing this gives the DB admins all privileges over all roles as well. Only superusers can grant the superuser privilege.

To grant superuser, do the following:

```sql
dev_database=# ALTER USER db_admin WITH SUPERUSER;
```

Run the following command to verify the privileges:

```sql
dev_database=# \du
```

```output
                                       List of roles
  Role name   |                         Attributes                         |   Member of
--------------+------------------------------------------------------------+---------------
 db_admin     | Superuser, Cannot login                                    | {engineering}
 developer    | Cannot login                                               | {engineering}
 engineering  | Cannot login                                               | {}
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 qa           | Cannot login                                               | {engineering}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

## 4. Revoke privileges from roles

To revoke superuser from the DB admins so that they can no longer change privileges for other roles, do the following:

```sql
dev_database=# ALTER USER db_admin WITH NOSUPERUSER;
```

Run the following command to verify the privileges:

```sql
dev_database=# \du
```

You should see the following output.

```output
                                       List of roles
  Role name   |                         Attributes                         |   Member of
--------------+------------------------------------------------------------+---------------
 db_admin     | Cannot login                                               | {engineering}
 developer    | Cannot login                                               | {engineering}
 engineering  | Cannot login                                               | {}
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 qa           | Cannot login                                               | {engineering}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```
