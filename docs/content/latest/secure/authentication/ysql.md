
## Overview

YSQL authentication is the process of identifying that a YSQL user is who they say they are, is based on roles. Users and roles can be created with superuser, non-superuser, and login privileges. Administrators can create users and roles using the [`CREATE ROLE`](../../api/ysql/ddl_create_role/) statement (or its alias, [`CREATE USER`](../../api/ysql/ddl_create_user/)). After users and roles have been created, the [`ALTER ROLE`](../../api/ysql/ddl_alter_role/) and [`DROP ROLE`](../../api/ysql/ddl_drop_role/) statements are used to change or remove users and roles. 

YSQL authorization is the process of access control created by granting or revoking privileges to YSQL users and roles, see [Authorization](../authorization). Privileges are managed using [`GRANT`](../../api/ysql/ddl_grant/), [`REVOKE`](../../api/ysql/ddl_revoke/), [`CREATE ROLE`](../../api/ysql/ddl_create_role/), [`ALTER ROLE`](../../api/ysql/ddl_alter_role/), and [`DROP ROLE`](../../api/ysql/ddl_drop_role/).

## Specify a password for the default user

When you start your YugabyteDB cluster, the YB-Master and YB-TServer services are launched using the default user, named `yugabyte`, and then this user is connected to the default database, also named `yugabyte`. When YSQL authentication is enabled, all users (including `yugabyte`) require a password to log into a YugabyteDB database. So, before starting up YugabyteDB with YSQL authentication enabled, you need to specify a password for the `yugabyte` user.

To add a password to the `yugabyte` user, launch `ysqlsh` and then run the following command, specifying a password of your choice.

```sql
yugabyte=# ALTER ROLE yugabyte with password 'yugabyte';
```

After creating a password for `yugabyte`, you can start, or restart, your YugabyteDB cluster with YSQL authentication enabled.

## Enable YSQL authentication

### yb-ctl

To enable YSQL authentication in your local YugabyteDB clusters, you can  use the `--tserver_flags` option with the `yb-ctl create` and `yb-ctl start` commands.

When you create a local cluster, you can run a command like this to enable YSQL authentication in the newly-created cluster.

```bash
./bin/yb-ctl create --tserver_flags ysql_enable_auth=true
```

After your local cluster has been created, you can enable YSQL authentication by starting your cluster with a command like this:

```bash
./bin/yb-ctl start --tserver_flags ysql_enable_auth=true
```

### yb-tserver

To enable YSQL authentication in deployable YugabyteDB clusters, you need to start your `yb-tserver` services using the `--ysql_enable_auth=true` flag. Your command should look similar to this command:

```
./bin/yb-tserver \
  --tserver_master_addrs <master addresses> \
  --fs_data_dirs <data directories> \
  --ysql_enable_auth=true \
  >& /home/centos/disk1/yb-tserver.out &
```

You can also enable YSQL authentication by adding the `--ysql_enable_auth=true` to the YB-TServer configuration file (`tserver.conf`). For more information, see [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

## Connect with the default admin credentials

A YugabyteDB cluster with authentication enabled starts with the default admin user of `yugabyte` and the default database of `yugabyte`. You can connect to the cluster and use the YSQL shell by running the following `ysqlsh` script from the YugabyteDB home directory:

```sh
$ ./bin/ysqlsh -U yugabyte
```

You will be prompted to enter the password. Upon successful login to YSQL shell, you will see the following, displaying the `yugabyte` user in the prompt.

```
ysqlsh (11.2-YB-2.0.0.0-b16)
Type "help" for help.

yugabyte=#
```

## Common user authentication tasks

### Creating users

To add a new user, run the [`CREATE ROLE` statement](../../api/ysql/commands/ddl_create_role/) or its alias, the `CREATE USER` statement. Users are roles that have the `LOGIN` privilege granted to them. Roles created with the `SUPERUSER` option in addition to the `LOGIN` option have full access to the database. Superusers can run all of the YSQL statements on any of the database resources.

**NOTE** By default, creating a role does not grant the `LOGIN` or the `SUPERUSER` privileges, these need to be explicitly granted.

#### Create a regular user

To add a new regular user (with NONSUPERUSER privileges) named `john`, with the password `PasswdForJohn`, and grant him `LOGIN` privileges, run the following command.

```sql
yugabyte=# CREATE ROLE john WITH LOGIN PASSWORD 'PasswdForJohn';
```

To verify the user account just created, you can run a query similar like this:

```sql
yugabyte=# SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

You should see the following output.

```
 role     | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
     john |      True |        False |          []
 yugabyte |      True |         True |          []

(2 rows)
```

#### Create a user with SUPERUSER status

The `SUPERUSER` status should be given only to a limited number of users. Applications should generally not access the database using an account that has the superuser privilege.

**NOTE** Only a role with the `SUPERUSER` privilege can create a new role with the `SUPERUSER` privilege, or grant it to an existing role.

To create a superuser `admin` with the `LOGIN` privilege, run the following command using a superuser account:

```sql
yugabyte=# CREATE ROLE admin WITH LOGIN SUPERUSER PASSWORD = 'PasswdForAdmin';
```

To verify the `admin` account just created, run the following query.

```sql
yugabyte=# SELECT rolname, rolsuper, rolcanlogin FROM pg_roles;
```

To see all of the information available in the `pg_roles` table, run `SELECT * from pg_roles`.

You should see a table output similar to this:

```
          rolname          | rolsuper | rolcanlogin 
---------------------------+----------+-------------
 postgres                  | t        | t
 ...
 yugabyte                  | t        | t
 steve                     | f        | t
 john                      | f        | t
(13 rows)
```

In this table, you can see that both `postgres` and `yugabyte` users can log in and have `SUPERUSER` status.

As an alternative, you can simply run the `\du` command to see this information in a simpler, easier-to-read format:

```bash
                                    List of roles
 Role name |                         Attributes                         | Member of  
-----------+------------------------------------------------------------+------------
 john      | Cannot login                                               | {}
 postgres  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 steve     | Superuser                                                  | {sysadmin}
 sysadmin  | Create role, Create DB                                     | {}
 yugabyte  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 ```

## 5. Connect to ysqlsh using non-default credentials

You can connect to a YSQL cluster with authentication enabled as follows:

```sh
$ ysqlsh -u <username> -p <password>
```

Alternatively, you can omit the `-p <password>` above and you will be prompted for a password.

As an example of connecting as a user, we can login with the credentials of the user `john` that we created above by running the following command and entering the password when prompted:

```sh
$ ysqlsh -u john
```

## 5. Edit user accounts

You can edit existing user accounts using the [ALTER ROLE](../../api/ysql/commands/ddl_alter_role/) command. Note that the role making these changes should have sufficient privileges to modify the target role.

### Changing password for a user

To change the password for `john` above, you can do:

```sql
yugabyte=# ALTER ROLE john WITH PASSWORD = 'new-password';
```

### Granting and removing superuser privileges

In the example above, we can verify that `john` is not a superuser:

```sql
yugabyte=# SELECT rolname, rolsuper, rolcanlogin FROM pg_roles WHERE rolname='john';
```

```
yugabyte=# SELECT rolname, rolsuper, rolcanlogin FROM pg_roles WHERE rolname='steve';
 rolname | rolsuper | rolcanlogin 
---------+----------+-------------
 john    | f        | t
(1 row)
```

To grant superuser privileges to `john`, run the following command.

```sql
yugabyte=# ALTER ROLE john WITH SUPERUSER = true;
```

We can now verify that john is now a superuser.

```sql
yugabyte=# SELECT rolname, rolsuper, rolcanlogin FROM pg_roles WHERE rolname='john';
```

```
 rolname | rolsuper | rolcanlogin 
---------+----------+-------------
 john    | t        | t
(1 row)
```

Similarly, you can revoke superuser privileges by running:

```sql
yugabyte=# ALTER ROLE john WITH NOSUPERUSER;
```

### Enable and disable login privileges

In the example above, we can verify that `john` can login to the database by doing the following:

```sql
yugabyte=# SELECT role, rolcanlogin FROM pg_roles WHERE role='john';
```

```
 role | canlogin | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |        False |          []

(1 rows)
```

To disable login privileges for `john`, run the following command.

```sql
yugabyte=# ALTER ROLE john WITH NOLOGIN;
```

You can verify this as follows.

```sql
yugabyte=# SELECT rolname, rolcanlogin FROM pg_roles WHERE rolname='john';
```

```
 rolname | rolcanlogin 
---------+-------------
 john    | f
(1 row)
```

Trying to login as `john` using `cqlsh` will throw the following error.

```bash
yugabyte=# ./bin/ysqlsh -U john
Password for user john:
```
After entering the correct password, John would see the following message:

```
ysqlsh: FATAL:  role "john" is not permitted to log in
```

To re-enable login privileges for `john`, run the following command.

```sql
yugabyte=#  ALTER ROLE john WITH LOGIN;
```

### Delete a user

You can delete a user with the [DROP ROLE](../../api/ysql/ddl_drop_role/) command.

For example, to drop the user `john` in the above example, run the following command as a superuser:

```sql
yugabyte=# DROP ROLE IF EXISTS john;
```

You can quickly verify that the `john` role was dropped by running the `\du` command:

```bash
yugabyte=# \du
```

```
                                    List of roles
 Role name |                         Attributes                         | Member of  
-----------+------------------------------------------------------------+------------
 postgres  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 sysadmin  | Create role, Create DB                                     | {}
 yugabyte  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

## Related topics

- [CREATE ROLE](../../api/ysql/ddl_create_role/)
- [ALTER ROLE](../../api/ysql/ddl_alter_role/)
- [DROP ROLE](../../api/ysql/ddl_drop_role/)
- [GRANT](../../api/ysql/ddl_grant/)
- [REVOKE](../../api/ysql/ddl_revoke/)