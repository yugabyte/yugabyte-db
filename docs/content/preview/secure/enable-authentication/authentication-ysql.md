---
title: Enable users in YSQL
headerTitle: Enable users in YSQL
description: Enable users in YSQL.
menu:
  preview:
    name: Enable users
    identifier: enable-authentication-1-ysql
    parent: enable-authentication
    weight: 715
aliases:
  - /preview/secure/enable-authentication/ysql/
type: docs
---

{{<api-tabs>}}

YSQL authentication, the process of identifying that YSQL users are who they say they are, is based on roles. Users, groups, and roles in YugabyteDB are created using roles. Typically, a role that has login privileges is known as a *user*, while a *group* is a role that can have multiple users as members.

Users, roles, and groups allow administrators to verify whether a particular user or role is authorized to create, access, change, or remove databases, or manage users and roles.

Authentication verifies the identity of a user while authorization determines the verified user's database access privileges.

[Authorization](../../authorization/) is the process of managing access control based on roles. For YSQL, enabling authentication automatically enables authorization and the [role-based access control (RBAC) model](../../authorization/rbac-model/), to determine the access privileges. Privileges are managed using [`GRANT`](../../../api/ysql/the-sql-language/statements/dcl_grant/), [`REVOKE`](../../../api/ysql/the-sql-language/statements/dcl_revoke/), [`CREATE ROLE`](../../../api/ysql/the-sql-language/statements/dcl_create_role/), [`ALTER ROLE`](../../../api/ysql/the-sql-language/statements/dcl_alter_role/), and [`DROP ROLE`](../../../api/ysql/the-sql-language/statements/dcl_drop_role/).

Users and roles can be created with superuser, non-superuser, and login privileges, and the roles that users have are used to determine what access privileges are available. Administrators can create users and roles using the [`CREATE ROLE`](../../../api/ysql/the-sql-language/statements/dcl_create_role/) statement (or its alias, [`CREATE USER`](../../../api/ysql/the-sql-language/statements/dcl_create_user/)). After users and roles have been created, [`ALTER ROLE`](../../../api/ysql/the-sql-language/statements/dcl_alter_role/) and [`DROP ROLE`](../../../api/ysql/the-sql-language/statements/dcl_drop_role/) statements are used to change or remove users and roles.

## Default user and password

When you start a YugabyteDB cluster, the YB-Master and YB-TServer services are launched using the default user, named `yugabyte`, and then this user is connected to the default database, also named `yugabyte`.

Once YSQL authentication is enabled, all users (including `yugabyte`) require a password to log in to a YugabyteDB database. The default `yugabyte` user has a default password of `yugabyte` that lets this user sign into YugabyteDB when YSQL authentication is enabled.

{{< note title="Note" >}}
Versions of YugabyteDB prior to 2.0.1 do not have a default password. In this case, before you start YugabyteDB with YSQL authentication enabled, you need to make sure that the `yugabyte` user has a password.

If you are using YugabyteDB 2.0 (and **not** 2.0.1 or later) and have not yet assigned a password to the `yugabyte` user, do the following:

1. With your YugabyteDB cluster up and running, [open `ysqlsh`](#open-the-ysql-shell-ysqlsh).
1. Run the following `ALTER ROLE` statement, specifying a password (`yugabyte` or a password of your choice):

    ```sql
    yugabyte=# ALTER ROLE yugabyte with password 'yugabyte';
    ```

{{< /note >}}

## Enable YSQL authentication

### Start local clusters

To enable YSQL authentication in your local YugabyteDB clusters, add the [--ysql_enable_auth](../../../reference/configuration/yugabyted/#start) flag with the `yugabyted start` command, as follows:

```sh
$ ./bin/yugabyted start --ysql_enable_auth=true
```

### Start YB-TServer services

To enable YSQL authentication in deployable YugabyteDB clusters, you need to start your `yb-tserver` services using the [--ysql_enable_auth](../../../reference/configuration/yb-tserver/#ysql-enable-auth) flag. Your command should look similar to the following:

```sh
./bin/yb-tserver \
  --tserver_master_addrs <master addresses> \
  --fs_data_dirs <data directories> \
  --ysql_enable_auth=true \
  >& /home/centos/disk1/yb-tserver.out &
```

You can also enable YSQL authentication by adding the `--ysql_enable_auth=true` to the YB-TServer configuration file (`tserver.conf`). For more information, refer to [Start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

## Open the YSQL shell (ysqlsh)

A YugabyteDB cluster with authentication enabled starts with the default admin user of `yugabyte` and the default database of `yugabyte`. You can connect to the cluster and use the [YSQL shell](../../../admin/ysqlsh/) by running the following `ysqlsh` command from the YugabyteDB home directory:

```sh
$ ./bin/ysqlsh -U yugabyte
```

You are prompted to enter the password. After logging in, you should see the following output:

```output
ysqlsh (11.2-YB-2.7.0.0-b0)
Type "help" for help.

yugabyte=#
```

## Common user authorization tasks

Here are some common authorization-related tasks. For more detailed information on authorization, refer to [Role-Based Access Control](../../authorization/).

For information on configuring authentication, refer to [Authentication](../../authentication/).

### Create users

To add a new user, run the [`CREATE ROLE` statement](../../../api/ysql/the-sql-language/statements/dcl_create_role/) or its alias, the `CREATE USER` statement. Users are roles that have the `LOGIN` privilege granted to them. Roles created with the `SUPERUSER` option in addition to the `LOGIN` option have full access to the database. Superusers can run all of the YSQL statements on any of the database resources.

By default, creating a role does not grant the `LOGIN` or the `SUPERUSER` privileges — these need to be explicitly granted.

#### Create a regular user

To add a new regular user (with non-superuser privileges) named `john`, with the password `PasswdForJohn`, and grant him `LOGIN` privileges, run the following `CREATE ROLE` command.

```sql
yugabyte=# CREATE ROLE john WITH LOGIN PASSWORD 'PasswdForJohn';
```

```output
CREATE ROLE
```

To verify the user account just created, you can run a query like this:

```sql
yugabyte=# SELECT rolname, rolsuper, rolcanlogin FROM pg_roles;
```

You should see the following output.

```output
          rolname          | rolsuper | rolcanlogin
---------------------------+----------+-------------
 postgres                  | t        | t
 pg_monitor                | f        | f
 pg_read_all_settings      | f        | f
 pg_read_all_stats         | f        | f
 pg_stat_scan_tables       | f        | f
 pg_signal_backend         | f        | f
 pg_read_server_files      | f        | f
 pg_write_server_files     | f        | f
 pg_execute_server_program | f        | f
 yb_extension              | f        | f
 yb_fdw                    | f        | f
 yugabyte                  | t        | t
 john                      | f        | t
(11 rows)
```

#### Create a user with SUPERUSER privileges

The `SUPERUSER` privilege should be given only to a limited number of users. Applications should generally not access the database using an account that has the superuser privilege.

Only a role with the `SUPERUSER` privilege can create a new role with the `SUPERUSER` privilege, or grant it to an existing role.

To create a superuser `admin` with the `LOGIN` privilege, run the following command using a superuser account:

```sql
yugabyte=# CREATE ROLE admin WITH LOGIN SUPERUSER PASSWORD 'PasswdForAdmin';
```

To verify the `admin` account just created, run the following query:

```sql
yugabyte=# SELECT rolname, rolsuper, rolcanlogin FROM pg_roles;
```

You should see a table output similar to the following:

```output
          rolname          | rolsuper | rolcanlogin
---------------------------+----------+-------------
 postgres                  | t        | t
 ...
 yugabyte                  | t        | t
 john                      | f        | t
 admin                     | t        | t
(12 rows)
```

(To see all of the information available in the `pg_roles` table, run `SELECT * from pg_roles`.)

In this table, you can see that `postgres`, `admin`, and `yugabyte` users can log in and have `SUPERUSER` status.

As an easier alternative, you can run the `\du` [meta-command](../../../admin/ysqlsh-meta-commands/) to see this information in a simpler format:

```sql
yugabyte=# \du
```

```output
                                   List of roles
 Role name |                         Attributes                         | Member of
-----------+------------------------------------------------------------+-----------
 admin     | Superuser                                                  | {}
 john      |                                                            | {}
 postgres  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yugabyte  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 ```

Users with `SUPERUSER` status display "Superuser" in the list of attributes for each role.

### Connect using non-default credentials

You can connect to a cluster with authentication enabled as follows:

```sh
$ ysqlsh -U <username>
```

You are prompted for a password.

For example, to log in with the credentials of the user `john` that you created, you would run the following command and enter the password when prompted:

```sh
$ ysqlsh -U john
```

### Edit user accounts

You can edit existing user accounts using the [ALTER ROLE](../../../api/ysql/the-sql-language/statements/dcl_alter_role/) command. The role making the change must have sufficient privileges to modify the target role.

#### Changing password for a user

To change the password for `john`, enter the following command:

```sql
yugabyte=# ALTER ROLE john PASSWORD 'new-password';
```

#### Granting and removing superuser privileges

To verify that `john` is not a superuser, use the following `SELECT` statement:

```sql
yugabyte=# SELECT rolname, rolsuper, rolcanlogin FROM pg_roles WHERE rolname='john';
```

```output
 rolname | rolsuper | rolcanlogin
---------+----------+-------------
 john    | f        | t
(1 row)
```

To grant `SUPERUSER` privileges to `john`, log in as a superuser and run the following `ALTER ROLE` command:

```sql
yugabyte=# ALTER ROLE john SUPERUSER;
```

Verify that `john` is now a superuser by running the `\du` command.

```sql
yugabyte=# \du
```

```output
                                   List of roles
 Role name |                         Attributes                         | Member of
-----------+------------------------------------------------------------+-----------
 admin     | Superuser                                                  | {}
 john      | Superuser                                                  | {}
 postgres  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yugabyte  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

{{< note title="Note" >}}

In YugabyteDB (just as in PostgreSQL), `SUPERUSER` status includes all of the following attributes: `CREATEROLE` ("Create role"), `CREATEDB` ("Create DB"), `REPLICATION` ("Replication"), and `BYPASSRLS` ("Bypass RLS"). Whether these attributes display or not, all superusers have these attributes.

{{< /note >}}

Similarly, you can revoke superuser privileges by running:

```sql
yugabyte=# ALTER ROLE john WITH NOSUPERUSER;
```

### Enable and disable login privileges

To verify that `john` can log in to the database, do the following:

```sql
yugabyte=# SELECT rolname, rolcanlogin FROM pg_roles WHERE rolname='john';
```

```output
 rolname | rolcanlogin
---------+-------------
 john    |  t
(1 rows)
```

To disable login privileges for `john`, run the following command:

```sql
yugabyte=# ALTER ROLE john WITH NOLOGIN;
```

You can verify this as follows:

```sql
yugabyte=# SELECT rolname, rolcanlogin FROM pg_roles WHERE rolname='john';
```

```output
 rolname | rolcanlogin
---------+-------------
 john    | f
(1 row)
```

Trying to log in as `john` using `ysqlsh` now fails:

```sh
$ ./bin/ysqlsh -U john
```

After entering the correct password, you see the following message:

```output
Password for user john:
ysqlsh: FATAL:  role "john" is not permitted to log in
```

To re-enable login privileges for `john`, run the following command.

```sql
yugabyte=# ALTER ROLE john WITH LOGIN;
```

### Delete a user

You can delete a user with the [DROP ROLE](../../../api/ysql/the-sql-language/statements/dcl_drop_role/) statement.

For example, to drop the user `john`, run the following command as a superuser:

```sql
yugabyte=# DROP ROLE john;
```

To verify that the `john` role was dropped, run the `\du` command:

```sql
yugabyte=# \du
```

```output
                                   List of roles
 Role name |                         Attributes                         | Member of
-----------+------------------------------------------------------------+-----------
 admin     | Superuser                                                  | {}
 postgres  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yugabyte  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

## Related topics

- [CREATE ROLE](../../../api/ysql/the-sql-language/statements/dcl_create_role/)
- [ALTER ROLE](../../../api/ysql/the-sql-language/statements/dcl_alter_role/)
- [DROP ROLE](../../../api/ysql/the-sql-language/statements/dcl_drop_role/)
- [GRANT](../../../api/ysql/the-sql-language/statements/dcl_grant/)
- [REVOKE](../../../api/ysql/the-sql-language/statements/dcl_revoke/)
