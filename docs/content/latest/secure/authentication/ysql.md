
## Overview

YSQL authentication, identifying that a YugabyteDB user is who they say they are, is based on roles. Roles can be created with superuser, non-superuser, and login privileges. Administrators can create new roles and alter, or drop, existing ones using YCQL commands (`CREATE USER`,`CREATE ROLE`,`GRANT`,and `REVOKE`).

## 1. Specify the default admin user and password

Before enabling YSQL authentication, you need to specify a password for the default `yugabyte` user. When authentication is enabled, the default of no password will not work to authenticate.

## 2. Enable YSQL authentication

### yb-ctl

To enable YSQL authentication in your local YugabyteDB clusters, you can  use the `--tserver_flags` option with the `yb-ctl create` and `yb-ctl start` commands.

When you create a local cluster, you can run a command like this to enable YSQL authentication in the newly-created cluster.

```bash
./bin/yb-ctl create --tserver_flags ysql_enable-auth=true
```

After your local cluster has been created, you can enable YSQL authentication by starting your cluster with a command like this:

```bash
./bin/yb-ctl start --tserver_flags ysql_enable-auth=true
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

For details on configuring YSQL authentication in the YB-TServer configuration file (`tserver.conf`), see [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

## 3. Connect with the default admin credentials

A new YugabyteDB cluster with authentication enabled starts using the default admin user, the default username and password for this admin user is `yugabyte`/`<no password>`. Note that this default user has `SUPERUSER` privilege. You can connect to this cluster using `ysqlsh` as follows:

```sh
$ ysqlsh -U yugabyte -p yugabyte
```

You should see the cluster connect and the following prompt:

```
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

## 4. Create a new user

Use the [CREATE ROLE statement](../../api/ysql/commands/ddl_create_role/) to create a new role. Users are roles that have the `LOGIN` privilege granted to them. Roles created with the `SUPERUSER` option in addition to the `LOGIN` option have full access to the database. Superusers can run all the YSQL commands on any of the database resources.

**NOTE** By default, creating a role does not grant the `LOGIN` or the `SUPERUSER` privileges, these need to be explicitly granted.

### Creating a user

For example, to create a regular user `john` with the password `PasswdForJohn` and grant login privileges, run the following command.

```sql
yugabyte=# CREATE ROLE IF NOT EXISTS john WITH PASSWORD = 'PasswdForJohn' AND LOGIN = true;
```

If the role `john` already existed, the above statement will not error out since we have added the `IF NOT EXISTS` clause. To verify the user account just created, run the following query:

```sql
yugabyte=# SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

You should see the following output.

```
 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
      john |      True |        False |          []
 yugabyte |      True |         True |          []

(2 rows)
```

### Creating a superuser

The `SUPERUSER` status should be given only to a limited number of users. Applications should generally not access the database using an account that has the superuser privilege.

**NOTE** Only a role with the `SUPERUSER` privilege can create a new role with the `SUPERUSER` privilege, or grant it to an existing role.

To create a superuser `admin` with the `LOGIN` privilege, run the following command using a superuser account:

```sql
yugabyte=# CREATE ROLE admin WITH PASSWORD = 'PasswdForAdmin' AND LOGIN = true AND SUPERUSER = true;
```

To verify the admin account just created, run the following query.

```sql
yugabyte=# SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

You should see the following output.

```
 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
     admin |      True |         True |          []
      john |      True |        False |          []
  yugabyte |      True |         True |          []

(3 rows)
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

As an example of connecting as the `admin` user, we can run the following command.

```sh
$ ysqlsh -u admin -p PasswdForAdmin
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
yugabyte=# SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';
```

```
 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |        False |          []

(1 rows)
```

To grant superuser privileges to `john`, run the following command.

```sql
yugabyte=# ALTER ROLE john WITH SUPERUSER = true;
```

We can now verify that john is now a superuser.

```sql
yugabyte=# SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';
```

```
 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |         True |          []

(1 rows)
```

Similarly, you can revoke superuser privileges by running:

```sql
yugabyte=# ALTER ROLE john WITH SUPERUSER = false;
```

### Enable and disable login privileges

In the example above, we can verify that `john` is can login to the database by doing the following:

```sql
yugabyte=# SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';
```

```
 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |        False |          []

(1 rows)
```

To disable login privileges for `john`, run the following command.

```sql
yugabyte=# ALTER ROLE john WITH LOGIN = false;
```

You can verify this as follows.

```sql
yugabyte=# SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';
```

```
 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |     False |        False |          []

(1 rows)
```

Trying to login as `john` using `cqlsh` will throw the following error.

```sh
$ cqlsh -u john
Password:
Connection error:
  ... message="john is not permitted to log in"
```

To re-enable login privileges for `john`, run the following command.

```sql
yugabyte=#  ALTER ROLE john WITH LOGIN = true;
```

## 6. Change default admin credentials

It is highly recommended to change at least the default password for the superadmin user in real world deployments to keep the database cluster secure.

As an example, let us say we want to change the `yugabyte` user's password from `yugabyte` to `new_password`. You can do that as follows:

```sql
yugabyte=# ALTER ROLE yugabyte WITH PASSWORD = 'new_password';
```

Connecting to the cluster with the default password would no longer work:

```
$ bin/cqlsh -u yugabyte -p yugabyte
Connection error:
  ... Provided username yugabyte and/or password are incorrect ...
```

You can now connect to the cluster using the new password:

```sh
$ cqlsh -u yugabyte -p new_password
```

## 7. Deleting a user

You can delete a user with the [DROP ROLE](../../api/ysql/ddl_drop_role/) command.

For example, to drop the user `john` in the above example, run the following command as a superuser:

```sql
yugabyte=# DROP ROLE IF EXISTS john;
```

You can verify that the `john` role was dropped as follows:

```sql
yugabyte=# SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

```
 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
     admin |      True |         True |          []
 yugabyte |      True |         True |          []

(2 rows)
```
