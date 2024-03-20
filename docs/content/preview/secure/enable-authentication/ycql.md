---
title: Enable users in YCQL
headerTitle: Enable users in YCQL
description: Enable users in YCQL.
menu:
  preview:
    name: Enable users
    identifier: enable-authentication-2-ycql
    parent: enable-authentication
    weight: 715
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li>
    <a href="../yedis/" class="nav-link">
      <i class="icon-redis" aria-hidden="true"></i>
      YEDIS
    </a>
  </li>
</ul>

YCQL authentication is based on roles. Roles can be created with superuser, non-superuser, and login privileges. New roles can be created, and existing ones altered or dropped by administrators using YCQL commands.

## Enable YCQL authentication

### Start local clusters

To enable YCQL authentication in your local YugabyteDB clusters, add the [--use_cassandra_authentication](../../../reference/configuration/yugabyted/#start) flag with the `yugabyted start` command, as follows:

```sh
$ ./bin/yugabyted start --use_cassandra_authentication=true
```

### Start YB-TServer services

To enable YCQL authentication in deployable YugabyteDB clusters, start the `yb-tserver` processes with the [--use_cassandra_authentication](../../../reference/configuration/yb-tserver/#use-cassandra-authentication) flag. Your command should look similar to the following:

```sh
./bin/yb-tserver \
  --tserver_master_addrs <master addresses> \
  --fs_data_dirs <data directories> \
  --use_cassandra_authentication=true \
  >& /home/centos/disk1/yb-tserver.out &
```

You can read more about bringing up the YB-TServers for a deployment in the section on [manual deployment of a YugabyteDB cluster](../../../deploy/manual-deployment/start-tservers/).

## Connect using the default admin credentials

A new YugabyteDB cluster with authentication enabled comes up with a default admin user, the default username/password for this admin user is `cassandra`/`cassandra`. Note that this default user has `SUPERUSER` privilege. You can connect to this cluster using `ycqlsh` as follows:

```sh
$ ./bin/ycqlsh -u cassandra -p cassandra
```

You should see the cluster connect and the following prompt:

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cassandra@ycqlsh>
```

## Create users

Use the [CREATE ROLE command](../../../api/ycql/ddl_create_role/) to create a new role. Users are roles that have the `LOGIN` privilege granted to them. Roles created with the `SUPERUSER` option in addition to the `LOGIN` option have full access to the database. Superusers can run all the YCQL commands on any of the database resources.

Note that by default, creating a role does not grant the `LOGIN` or the `SUPERUSER` privileges, these need to be explicitly granted.

### Create a user

For example, to create a regular user `john` with the password `PasswdForJohn` and grant login privileges, run the following command.

```cql
cassandra@ycqlsh> CREATE ROLE IF NOT EXISTS john WITH PASSWORD = 'PasswdForJohn' AND LOGIN = true;
```

If the role `john` already existed, the above statement will not error out as you have added the `IF NOT EXISTS` clause. To verify the user account just created, run the following query:

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

You should see the following output.

```output
 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
      john |      True |        False |          []
 cassandra |      True |         True |          []

(2 rows)
```

### Create a superuser

The `SUPERUSER` status should be given only to a limited number of users. Applications should generally not access the database using an account that has the superuser privilege.

Only a role with the `SUPERUSER` privilege can create a new role with the `SUPERUSER` privilege, or grant it to an existing role.

To create a superuser `admin` with the `LOGIN` privilege, run the following command using a superuser account:

```cql
cassandra@ycqlsh> CREATE ROLE admin WITH PASSWORD = 'PasswdForAdmin' AND LOGIN = true AND SUPERUSER = true;
```

To verify the admin account just created, run the following query.

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

You should see the following output.

```output
 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
     admin |      True |         True |          []
      john |      True |        False |          []
 cassandra |      True |         True |          []

(3 rows)
```

#### Reset password for a superuser

Superusers can alter the passwords of other users including the admin user. However, if you have lost or forgotten the password of the only superuser, you can reset the password. To do this, you need to set the [ycql_allow_non_authenticated_password_reset](../../../reference/configuration/yb-tserver/#ycql-allow-non-authenticated-password-reset) YB-TServer flag to true.
To enable the password reset feature, you must first set the [`use_cassandra_authentication`](../../../reference/configuration/yb-tserver/#use-cassandra-authentication) flag to false.

For example, to reset the password for the admin superuser created in [Create a superuser](#create-a-superuser), do the following:

1. Set `use_cassandra_authentication` to `false` and `ycql_allow_non_authenticated_password_reset` to `true` as follows:

    ```sh
    ./bin/yb-tserver \
      --tserver_master_addrs <master addresses> \
      --use_cassandra_authentication=false \
      --ycql_allow_non_authenticated_password_reset=true
    ```

1. Change the password using `ycqlsh` as follows:

    ```cql
    cassandra@ycqlsh> ALTER ROLE admin WITH PASSWORD = <updatedPassword>
    ```

1. Set `use_cassandra_authentication` to `true` and `ycql_allow_non_authenticated_password_reset` to `false` as follows:

    ```sh
    ./bin/yb-tserver \
      --tserver_master_addrs <master addresses> \
      --use_cassandra_authentication=true \
      --ycql_allow_non_authenticated_password_reset=false
    ```

1. Log in to `ycqlsh` with the updated password as follows:

    ```sh
    ./bin/ycqlsh -u admin -p <updatedPassword>
    ```

## Connect using non-default credentials

You can connect to a YCQL cluster with authentication enabled as follows:

```sh
$ ./bin/ycqlsh -u <username> -p <password>
```

Alternatively, you can omit the `-p <password>` above and you will be prompted for a password.

As an example of connecting as a user, you can log in with the credentials of the user `john` that you created above by running the following command and entering the password when prompted:

```sh
$ ./bin/ycqlsh -u john
```

As an example of connecting as the `admin` user, you can run the following command.

```sh
$ ./bin/ycqlsh -u admin -p PasswdForAdmin
```

## Edit user accounts

You can edit existing user accounts using the [ALTER ROLE](../../../api/ycql/ddl_alter_role/) statement. Note that the role making these changes should have sufficient privileges to modify the target role.

### Change the password for a user

To change the password for `john` above, you can do:

```cql
cassandra@ycqlsh> ALTER ROLE john WITH PASSWORD = 'new-password';
```

### Grant and remove superuser privileges

In the example above, you can verify that `john` is not a superuser:

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';
```

```output
 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |        False |          []

(1 rows)
```

To grant superuser privileges to `john`, run the following command.

```cql
cassandra@ycqlsh> ALTER ROLE john WITH SUPERUSER = true;
```

We can now verify that john is now a superuser.

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';
```

```output
 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |         True |          []

(1 rows)
```

Similarly, you can revoke superuser privileges by running:

```cql
cassandra@ycqlsh> ALTER ROLE john WITH SUPERUSER = false;
```

### Enable and disable login privileges

In the example above, you can verify that `john` can log in to the database by doing the following:

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';
```

```output
 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |        False |          []

(1 rows)
```

To disable login privileges for `john`, run the following command.

```cql
cassandra@ycqlsh> ALTER ROLE john WITH LOGIN = false;
```

You can verify this as follows.

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';
```

```output
 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |     False |        False |          []

(1 rows)
```

Trying to log in as `john` using `ycqlsh` will throw the following error.

```sh
$ ./bin/ycqlsh -u john
Password:
Connection error:
  ... message="john is not permitted to log in"
```

To re-enable login privileges for `john`, run the following command.

```cql
cassandra@ycqlsh> ALTER ROLE john WITH LOGIN = true;
```

## Change default admin credentials

It is highly recommended to change at least the default password for the superuser in real world deployments to keep the database cluster secure.

For example, to change the `cassandra` user password from `cassandra` to `new_password`, do the following:

```cql
cassandra@ycqlsh> ALTER ROLE cassandra WITH PASSWORD = 'new_password';
```

Connecting to the cluster with the default password no longer works:

```sh
$ ./bin/ycqlsh -u cassandra -p cassandra
```

```output
Connection error:
  ... Provided username 'cassandra' and/or password are incorrect ...
```

You can now connect to the cluster using the new password:

```sh
$ ./bin/ycqlsh -u cassandra -p new_password
```

## Delete users

You can delete a user with the [DROP ROLE](../../../api/ycql/ddl_drop_role/) command.

For example, to drop the user `john` in the above example, run the following command as a superuser:

```cql
cassandra@ycqlsh> DROP ROLE IF EXISTS john;
```

You can verify that the `john` role was dropped as follows:

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

```output
 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
     admin |      True |         True |          []
 cassandra |      True |         True |          []

(2 rows)
```
