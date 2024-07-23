---
title: Default database reference
headerTitle: Default databases in YSQL
linkTitle: Default databases
description: Default databases for YugabyteDB.
menu:
  preview:
    identifier: default-databases
    parent: configuration
    weight: 3200
type: docs
---

When a YugabyteDB cluster is deployed, YugabyteDB creates a set of PostgreSQL databases.

To list all databases, use the `\l` meta-command.

```sql
yugabyte=# \l
```

```output
                                   List of databases
      Name       |  Owner   | Encoding | Collate |    Ctype    |   Access privileges
-----------------+----------+----------+---------+-------------+-----------------------
 postgres        | postgres | UTF8     | C       | en_US.UTF-8 |
 system_platform | postgres | UTF8     | C       | en_US.UTF-8 |
 template0       | postgres | UTF8     | C       | en_US.UTF-8 | =c/postgres          +
                 |          |          |         |             | postgres=CTc/postgres
 template1       | postgres | UTF8     | C       | en_US.UTF-8 | =c/postgres          +
                 |          |          |         |             | postgres=CTc/postgres
 yugabyte        | postgres | UTF8     | C       | en_US.UTF-8 |
(6 rows)
```

The following table describes the default PostgreSQL databases created by YugabyteDB when deploying a cluster.

| Database | Source | Description |
| :--- | :--- | :--- |
| postgres | PostgreSQL | PostgreSQL default database meant for use by users, utilities, and third party applications. |
| system_platform | YugabyteDB | Used by [YugabyteDB Anywhere](../../../yugabyte-platform/) to run periodic read and write tests to check the health of the node's YSQL endpoint. |
| template0 | PostgreSQL | [PostgreSQL template database](https://www.postgresql.org/docs/current/manage-ag-templatedbs.html), to be copied when using CREATE DATABASE commands. template0 should never be modified. |
| template1 | PostgreSQL | [PostgreSQL template database](https://www.postgresql.org/docs/current/manage-ag-templatedbs.html), copied when using CREATE DATABASE commands. You can add objects to template1; these are copied into databases created later. |
| yugabyte | YugabyteDB | The default database for YSQL API connections. See [Default user](../../../secure/enable-authentication/authentication-ysql/#default-user-and-password). |

For more information on the default PostgreSQL databases, refer to [Managing Databases](https://www.postgresql.org/docs/11/managing-databases.html) on the PostgreSQL documentation.

## See also

- [Default YSQL roles and users](../../../secure/authorization/rbac-model/)
