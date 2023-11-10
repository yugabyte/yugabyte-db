---
title: Database authorization
headerTitle: Database authorization
linkTitle: Database authorization
description: Use the role-based access control (RBAC) to manage universe users and roles.
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: authorization-platform
    weight: 27
type: docs
---

When you deploy a universe, you set up the database admin credentials, which you use to access the YugabyteDB database installed on your cluster. Use this account to:

- add more database users
- assign privileges to users
- change your password, or the passwords of other users

YugabyteDB uses [role-based access control](../../../secure/authorization/) (RBAC) to manage database authorization. A database user's access is determined by the roles they are assigned. You should grant database users only the privileges that they require.

For information on managing your YugabyteDB Anywhere instance users, refer to [Manage account users](../../administer-yugabyte-platform/rbac-platform/).

The YugabyteDB database on your universe includes a set of default users and roles in YSQL and YCQL.

## YSQL default roles and users

To view the roles in your universe, enter the following command:

```sql
yugabyte=> \du
```

```output
                                     List of roles
  Role name   |                         Attributes                         | Member of 
--------------+------------------------------------------------------------+-----------
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yb_db_admin  | No inheritance, Cannot login                               | {}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

The following table describes the default YSQL roles and users in YugabyteDB Managed clusters.

<!-- Portions of this table are also under RBAC in core docs -->

| Role | Description |
| --- | --- |
| yugabyte | The default user for your database. This user is a superuser. |
| postgres | Superuser role created during database creation. |

## YCQL default roles and users

In YCQL, there is a single superuser called `cassandra` used during database creation. The default user (by default, `admin`) added when you created the universe has superuser privileges in YCQL. As a superuser, you can delete the `cassandra` user if you choose to.

## Learn more

- [Manage Users and Roles in YugabyteDB](../../../secure/authorization/create-roles/)
- [Role-based access control](../../../secure/authorization/)
- [PostgreSQL extensions](../../../explore/ysql-language-features/pg-extensions/)
