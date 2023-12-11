---
title: Database authorization
linkTitle: Database authorization
description: The default YugabyteDB users and roles available in YugabyteDB Managed clusters.
headcontent: Default YugabyteDB users and roles in YugabyteDB Managed clusters
menu:
  preview_yugabyte-cloud:
    identifier: cloud-users
    parent: cloud-secure-clusters
    weight: 300
type: docs
---

To manage database access and authorization, YugabyteDB uses [role-based access control](../../../secure/authorization/) (RBAC), consisting of a collection of privileges on resources given to roles.

(For information on managing your YugabyteDB Managed account users, refer to [Manage account users](../../managed-security/manage-access/).)

The YugabyteDB database on your YugabyteDB Managed cluster includes a set of default users and roles in YSQL and YCQL.

## YSQL default roles and users

To view the roles in your cluster, enter the following command:

```sql
yugabyte=> \du
```

```output
                                                         List of roles
  Role name   |                         Attributes                         |                     Member of
--------------+------------------------------------------------------------+---------------------------------------------------------------
 admin        | Create role, Create DB, Bypass RLS                         | {yb_superuser}
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yb_db_admin  | No inheritance, Cannot login                               | {}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yb_superuser | Create role, Create DB, Cannot login, Bypass RLS           | {pg_read_all_stats,pg_signal_backend,yb_extension,yb_db_admin}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

The following table describes the default YSQL roles and users in YugabyteDB Managed clusters.

<!-- Portions of this table are also under RBAC in core docs -->

| Role | Description |
| --- | --- |
| [admin](#admin-and-yb-superuser) | The default user for your cluster. If you added your own credentials during cluster creation, the user name will be the one you provided. Although not a superuser, this role is a member of yb_superuser, and you can use it to perform database operations, create other yb_superuser users, create extensions, and manage your cluster. |
| postgres | Superuser role created during database creation. Not available to YugabyteDB Managed users. |
| [yb_db_admin](#yb-db-admin) | Role that allows non-superuser users to create tablespaces and perform other privileged operations. |
| [yb_extension](#yb-extension) | Role that allows non-superuser users to create PostgreSQL extensions. |
| yb_fdw | Role that allows non-superuser users to [CREATE](../../../api/ysql/the-sql-language/statements/ddl_create_foreign_data_wrapper/), [ALTER](../../../api/ysql/the-sql-language/statements/ddl_alter_foreign_data_wrapper/), and [DROP](../../../api/ysql/the-sql-language/statements/ddl_drop_foreign_data_wrapper/) [foreign data wrappers](../../../explore/ysql-language-features/advanced-features/foreign-data-wrappers/). |
| [yb_superuser](#admin-and-yb-superuser) | YugabyteDB Managed only role. This role is assigned to the default cluster user (that is, admin) to perform all the required operations on the database, including creating other yb_superuser users. For security reasons, yb_superuser doesn't have YugabyteDB superuser privileges. |
| yugabyte | Superuser role used during database creation, by Yugabyte support to perform maintenance operations, and for backups (ysql_dumps). Not available to YugabyteDB Managed users. |

### Admin and yb_superuser

When creating a YugabyteDB cluster in YugabyteDB Managed, you set up the credentials for your database admin user. For security reasons, this user does not have YugabyteDB superuser privileges; it is instead a member of `yb_superuser`, a role specific to YugabyteDB Managed clusters.

Although not a superuser, `yb_superuser` includes sufficient privileges to perform all the required operations on a database, including creating other yb_superuser users, as follows:

- Has the following role options: `INHERIT`, `CREATEROLE`, `CREATEDB`, and `BYPASSRLS`.

- Member of the following roles: `pg_read_all_stats`, `pg_signal_backend`, `yb_db_admin`, and [yb_extension](#yb-extension).

`yb_superuser` is the highest privileged role you have access to in YugabyteDB Managed. You can't delete, change the passwords, or login using the `postgres` or `yugabyte` superuser roles.

### yb_db_admin

The `yb_db_admin` role is specific to YugabyteDB Managed clusters and allows non-superuser roles to perform a number of privileged operations in a manner similar to a superuser, such as performing operations on objects it does not own.

`yb_superuser` and, by extension, the default database admin user, is a member of `yb_db_admin`.

### yb_extension

The `yb_extension` role allows non-superuser roles to [create extensions](../../cloud-clusters/add-extensions/). A user granted this role can create all the extensions that are bundled in YugabyteDB. `yb_superuser` and, by extension, the default database admin user, is a member of `yb_extension`.

## YCQL default roles and users

In YCQL, there is a single superuser called `cassandra` used during database creation. The default user (by default, `admin`) added when you created the cluster has superuser privileges in YCQL. As a superuser, you can delete the cassandra user if you choose to.

## Learn more

- [Manage Users and Roles in YugabyteDB](../../../secure/authorization/create-roles/)
- [Role-based access control](../../../secure/authorization/)
- [PostgreSQL extensions](../../../explore/ysql-language-features/pg-extensions/)
- [Create YSQL extensions in YugabyteDB Managed](../../cloud-clusters/add-extensions/)

## Next step

- [Add database users](../add-users/)
