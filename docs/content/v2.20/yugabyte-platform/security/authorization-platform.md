---
title: Database authorization
headerTitle: Database authorization
linkTitle: Database authorization
description: Use the role-based access control (RBAC) to manage universe users and roles.
menu:
  v2.20_yugabyte-platform:
    parent: security
    identifier: authorization-platform
    weight: 30
type: docs
---

When you deploy a universe, you can set up the database admin credentials for YSQL and YCQL, which you use to access the YugabyteDB database installed on your universe. Use this account to:

- add more database users
- assign privileges to users
- change your password, or the passwords of other users

YugabyteDB uses [role-based access control](../../../secure/authorization/) (RBAC) to manage database authorization. A database user's access is determined by the roles they are assigned. You should grant database users only the privileges that they require.

(For information on managing access to your YugabyteDB Anywhere instance, refer to [Manage account users](../../administer-yugabyte-platform/anywhere-rbac/).)

## Enable database authentication

You enable the YSQL and YCQL endpoints and database authentication when deploying a universe.

On the **Create Universe > Primary Cluster** page, under **Security Configurations**, enable the **Authentication Settings** for the APIs you want to use, as shown in the following illustration.

![Enable YSQL and YCQL endpoints](/images/yp/security/enable-endpoints.png)

Enter the password to use for the default database admin superuser (`yugabyte` for YSQL, and `cassandra` for YCQL).

You can also enable and disable the endpoints and authentication after deployment. Navigate to your universe, click **Actions**, and choose **Edit YSQL Configuration** or **Edit YCQL Configuration**.

Note that for universes deployed using YugabyteDB Anywhere, you can't exclusively [enable authentication using flags](../../../secure/enable-authentication/authentication-ysql/). You must enable and disable authentication using the YugabyteDB Anywhere UI.

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

For more information, see [YSQL roles](../../../secure/authorization/rbac-model/#roles).

### YCQL default roles and users

In YCQL, there is a single superuser called `cassandra` used during database creation. For more information, see [YCQL roles](../../../secure/authorization/rbac-model-ycql/#roles).

## Create and manage database users and roles

To manage database users, first [connect to your universe](../../create-deployments/connect-to-universe/).

To create and manage database roles and users (users are roles with login privileges), use the following statements:

| I want to | YSQL Statement | YCQL Statement |
| :--- | :--- | :--- |
| Create a user or role. | [CREATE ROLE](../../../api/ysql/the-sql-language/statements/dcl_create_role/) | [CREATE ROLE](../../../api/ycql/ddl_create_role/) |
| Delete a user or role. | [DROP ROLE](../../../api/ysql/the-sql-language/statements/dcl_drop_role/) | [DROP ROLE](../../../api/ycql/ddl_drop_role/) |
| Assign privileges to a user or role. | [GRANT](../../../api/ysql/the-sql-language/statements/dcl_grant/) | [GRANT ROLE](../../../api/ycql/ddl_grant_role/) |
| Remove privileges from a user or role. | [REVOKE](../../../api/ysql/the-sql-language/statements/dcl_revoke/) | [REVOKE ROLE](../../../api/ycql/ddl_revoke_role/) |
| Change your own or another user's password. | [ALTER ROLE](../../../api/ysql/the-sql-language/statements/dcl_alter_role/) | [ALTER ROLE](../../../api/ycql/ddl_alter_role/) |

## Learn more

- [Manage users and roles in YugabyteDB](../../../secure/authorization/create-roles/)
