---
title: Add database users
linkTitle: Add database users
description: Add users to YugabyteDB Managed clusters
headcontent: Grant team members and applications access to your database
menu:
  preview_yugabyte-cloud:
    identifier: add-users
    parent: cloud-secure-clusters
    weight: 350
type: docs
---

When you create a cluster in YugabyteDB Managed, you set up the database admin credentials, which you use to access the YugabyteDB database installed on your cluster. Use this account to:

- add more database users
- assign privileges to users
- change your password, or the passwords of other users

YugabyteDB uses [role-based access control](../../../secure/authorization/) (RBAC) to [manage database authorization](../cloud-users/). A database user's access is determined by the roles they are assigned. You should grant database users only the privileges that they require.

## Create and manage users and roles

To manage database users, first connect to your cluster using [Cloud Shell](../../cloud-connect/connect-cloud-shell/) or a [client shell](../../cloud-connect/connect-client-shell/).

To create and manage database roles and users (users are roles with login privileges), use the following statements:

| I want to | YSQL Statement | YCQL Statement |
| :--- | :--- | :--- |
| Create a user or role. | [CREATE ROLE](../../../api/ysql/the-sql-language/statements/dcl_create_role/) | [CREATE ROLE](../../../api/ycql/ddl_create_role/) |
| Delete a user or role. | [DROP ROLE](../../../api/ysql/the-sql-language/statements/dcl_drop_role/) | [DROP ROLE](../../../api/ycql/ddl_drop_role/) |
| Assign privileges to a user or role. | [GRANT](../../../api/ysql/the-sql-language/statements/dcl_grant/) | [GRANT ROLE](../../../api/ycql/ddl_grant_role/) |
| Remove privileges from a user or role. | [REVOKE](../../../api/ysql/the-sql-language/statements/dcl_revoke/) | [REVOKE ROLE](../../../api/ycql/ddl_revoke_role/) |
| Change your own or another user's password. | [ALTER ROLE](../../../api/ysql/the-sql-language/statements/dcl_alter_role/) | [ALTER ROLE](../../../api/ycql/ddl_alter_role/) |

### Create a database user

Add database users as follows:

1. Add the user using the CREATE ROLE statement.
1. Grant the user any roles they require using the GRANT statement.
1. Authorize their network so that they can access the cluster. Refer to [Assign IP allow lists](../../cloud-secure-clusters/add-connections/).
1. Send them the credentials.

{{< note title="Database users and case sensitivity" >}}

Like SQL and CQL, both YSQL and YCQL are case-insensitive by default. When specifying an identifier, such as the name of a table or role, YSQL and YCQL automatically convert the identifier to lowercase. For example, `CREATE ROLE Alice` creates the role "alice". To use a case-sensitive name, enclose the name in quotes. For example, to create the role "Alice", use `CREATE ROLE "Alice"`.

{{< /note >}}

#### YSQL

To add a database user in YSQL, use the CREATE ROLE statement as follows:

```sql
yugabyte=# CREATE ROLE <username> WITH LOGIN PASSWORD '<password>';
```

To grant a role to a user, use the GRANT statement as follows:

```sql
yugabyte=# GRANT <rolename> TO <username>;
```

{{< note title="Note" >}}
You can't create YSQL superusers in YugabyteDB Managed. To create another database administrator, grant the `yb_superuser` role. Refer to [Database authorization in YugabyteDB Managed clusters](../cloud-users/).
{{< /note >}}

#### YCQL

To add a database user in YCQL, use the CREATE ROLE statement as follows:

```sql
admin@ycqlsh> CREATE ROLE <username> WITH PASSWORD = '<password>' AND LOGIN = true;
```

To grant a role to a user, use the GRANT ROLE statement as follows:

```sql
admin@ycqlsh> GRANT ROLE <rolename> to <username>;
```

### Change a user password

To change your own or another user's password, use the ALTER ROLE statement.

In YSQL, enter the following:

```sql
yugabyte=# ALTER ROLE <username> PASSWORD 'new-password';
```

In YCQL, enter the following:

```sql
cassandra@ycqlsh> ALTER ROLE <username> WITH PASSWORD = 'new-password';
```

## Learn more

- [Manage users and roles in YugabyteDB](../../../secure/authorization/create-roles/)
- [Database authorization in YugabyteDB Managed clusters](../cloud-users/)

## Next steps

- [Connect using a shell](../../cloud-connect/connect-client-shell/)
- [Connect an application](../../cloud-connect/connect-applications/)
