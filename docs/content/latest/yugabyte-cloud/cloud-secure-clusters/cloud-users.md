---
title: Database authorization in Yugabyte Cloud clusters
linkTitle: Database authorization
description: The default YugabyteDB users and roles available in Yugabyte Cloud clusters.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-users
    parent: cloud-secure-clusters
    weight: 300
isTocNested: true
showAsideToc: true
---

To manage database access and authorization, YugabyteDB uses [role-based access control](../../../secure/authorization) (RBAC), consisting of a collection of privileges on resources given to roles.

Clusters in Yugabyte Cloud include a set of default users and roles in YSQL and YCQL.

## YSQL default roles and users

To view the roles in your cluster, enter the following command:

```sql
yugabyte=> \du
```

```output
                                                         List of roles
  Role name   |                         Attributes                         |                     Member of                      
--------------+------------------------------------------------------------+----------------------------------------------------
 admin        | Create role, Create DB, Bypass RLS                         | {yb_superuser}
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yb_extension | Cannot login                                               | {}
 yb_superuser | Create role, Create DB, Cannot login, Bypass RLS           | {pg_read_all_stats,pg_signal_backend,yb_extension}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

The following table describes the default YSQL roles and users in Yugabyte Cloud clusters.

<!-- Portions of this table are also under RBAC in core docs -->

| Role | Description |
| --- | --- |
| admin | The default user for your cluster. If you added your own credentials during cluster creation, the user name will be the one you entered. Although not a Superuser, this role is a member of yb_superuser, and you can use it to perform database operations, create other yb_superuser users, create extensions, and manage your cluster. |
| postgres | Superuser role created during database creation. Not available to cloud users. |
| yb_extension | Role that allows non-Superuser users to create PostgreSQL extensions. |
| yb_superuser | Yugabyte Cloud only role. This role is assigned to the default cluster user (that is, admin) to perform all the required operations on the database, including creating other yb_superuser users. For security reasons, yb_superuser does not have YugabyteDB Superuser privileges. |
| yugabyte | Superuser role used during database creation, by Yugabyte support to perform maintenance operations, and for backups (ysql_dumps). Not available to cloud users. |

### Admin and yb_superuser

When creating a YugabyteDB cluster in Yugabyte Cloud, you set up the credentials for your admin user. For security reasons, this user does not have YugabyteDB Superuser privileges; it is instead a member of `yb_superuser`, a role specific to Yugabyte Cloud clusters. `yb_superuser` is the highest privileged user you have access to in Yugabyte Cloud. You can create other users with the `yb_superuser` role.

You can't delete, change the passwords, or login using the `postgres` or `yugabyte` Superuser roles.

The `yb_superuser` role is created with the following options: `INHERIT`, `CREATEROLE`, `CREATEDB`, and `BYPASSRLS`.

Additionally `yb_superuser` is also a member of the following roles: `pg_read_all_stats`, `pg_signal_backend`, and `yb_extension`.

### yb_extension

The `yb_extension` role allows non-Superuser roles to [create extensions](../../cloud-clusters/add-extensions/). A user granted this role can create all the extensions that are bundled in YugabyteDB. `yb_superuser` and, by extension, the default admin user, is a member of `yb_extension`.

## YCQL default roles and users

In YCQL, there is a single Superuser called `cassandra` used during database creation. The default user (by default, `admin`) added when you created the cluster has Superuser privileges in YCQL. As a Superuser, you can delete the cassandra user if you choose to.

## Learn more

- [Add database users](../add-users/)
- [Manage Users and Roles in YugabyteDB](../../../secure/authorization/create-roles/)
- [Role-based access control](../../../secure/authorization/)
- [Pre-bundled extensions](../../../explore/ysql-language-features/extensions/)
- [Create YSQL extensions in Yugabyte Cloud](../../cloud-clusters/add-extensions/)
