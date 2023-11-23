---
title: Database authorization
headerTitle: Database authorization
linkTitle: Database authorization
description: Use the role-based access control (RBAC) to manage universe users and roles.
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: authorization-platform
    weight: 30
type: docs
---

When you deploy a universe, you set up the database admin credentials for YSQL and YCQL, which you use to access the YugabyteDB database installed on your universe. Use this account to:

- add more database users
- assign privileges to users
- change your password, or the passwords of other users

YugabyteDB uses [role-based access control](../../../secure/authorization/) (RBAC) to manage database authorization. A database user's access is determined by the roles they are assigned. You should grant database users only the privileges that they require.

(For information on managing your YugabyteDB Anywhere instance users, refer to [Manage account users](../../administer-yugabyte-platform/anywhere-rbac/).)

## Enable database authorization

You enable the YSQL and YCQL endpoints and database authorization when deploying a universe.

On the **Create Universe > Primary Cluster** page, under **Security Configurations**, enable the **Authentication Settings** for the APIs you want to use, as shown in the following illustration.

![Enable YSQL and YCQL endpoints](/images/yp/security/enable-endpoints.png)

Enter the password to use for the default admin superuser (`yugabyte` for YSQL, and `cassandra` for YCQL).

You can also enable and disable the endpoints and authorization after deployment. Navigate to your universe, click **Actions**, and choose **Edit YSQL Configuration** or **Edit YCQL Configuration**.

## Default roles and users

The YugabyteDB database on your universe includes a set of default users and roles in YSQL and YCQL.

### YSQL default roles and users

To view the YSQL roles in your universe, enter the following command:

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

For more information, see [YSQL roles](../../secure/authorization/rbac-model/#roles).

### YCQL default roles and users

In YCQL, there is a single superuser called `cassandra` used during database creation. For more information, see [YCQL roles](../../secure/authorization/rbac-model-ycql/#roles).

## Learn more

- [Role-based access overview](../../../secure/authorization/rbac-model/)
- [Manage users and roles in YugabyteDB](../../../secure/authorization/create-roles/)
- [Role-based access control](../../../secure/authorization/)
- [PostgreSQL extensions](../../../explore/ysql-language-features/pg-extensions/)
