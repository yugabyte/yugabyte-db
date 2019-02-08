
## Overview

YCQL authentication is based on roles. Roles can be created with superuser, non-superuser and login privileges. New roles can be created, and existing ones altered or dropped by administrators using CQL commands.


## 1. Enable YCQL authentication

You can enable access control by starting the `yb-tserver` processes with the `--use_cassandra_authentication=true` flag. Your command should look similar to that shown below:

```
./bin/yb-tserver \
  --tserver_master_addrs <master addresses> \
  --fs_data_dirs <data directories> \
  --use_cassandra_authentication=true \
  >& /home/centos/disk1/yb-tserver.out &
```

You can read more about bringing up the YB-TServers for a deployment in the section on [manual deployment of a YugaByte DB cluster](../../deploy/manual-deployment/start-tservers/).


## 2. Connect with the default admin credentials

A new YugaByte DB cluster with authentication enabled comes up with a default admin user, the default username/password for this admin user is `cassandra`/`cassandra`. Note that this default user has `SUPERUSER` privilege. You can connect to this cluster using `cqlsh` as follows:
<div class='copy separator-dollar'>
```sh
$ cqlsh -u cassandra -p cassandra
```
</div>

You should see the cluster connect and the following prompt:
```{.sql}
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cassandra@cqlsh>
```

## 3. Create a new user

Use the [CREATE ROLE statement](../../api/ycql/ddl_create_role/) to create a new role. Users are roles that have the `LOGIN` privilege granted to them. Roles created with the `SUPERUSER` option in addition to the `LOGIN` option have full access to the database. Superusers can run all the CQL commands on any of the database resources.

**NOTE** By default, creating a role does not grant the `LOGIN` or the `SUPERUSER` privileges, these need to be explicitly granted. 

### Creating a user

For example, to create a regular user `john` with the password `PasswdForJohn` and grant login privileges, run the following command.
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> CREATE ROLE IF NOT EXISTS john WITH PASSWORD = 'PasswdForJohn' AND LOGIN = true;
```
</div>

If the role `john` already existed, the above statement will not error out since we have added the `IF NOT EXISTS` clause. To verify the user account just created, run the following query:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```
</div>

You should see the following output:
```
 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
      john |      True |        False |          []
 cassandra |      True |         True |          []

(2 rows)
```



### Creating a superuser

The `SUPERUSER` status should be given only to a limited number of users. Applications should generally not access the database using an account that has the superuser privilege.

**NOTE** Only a role with the `SUPERUSER` privilege can create a new role with the `SUPERUSER` privilege, or grant it to an existing role.

To create a superuser `admin` with the `LOGIN` privilege, run the following command using a superuser account:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> CREATE ROLE admin WITH PASSWORD = 'PasswdForAdmin' AND LOGIN = true AND SUPERUSER = true;
```
</div>

To verify the admin account just created, run the following query:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```
</div>

You should see the following output:
```
 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
     admin |      True |         True |          []
      john |      True |        False |          []
 cassandra |      True |         True |          []

(3 rows)
```


## 4. Connect to cqlsh using non-default credentials

You can connect to a YCQL cluster with authentication enabled as follows:

```
$ cqlsh -u <username> -p <password>
```

Alternatively, you can omit the `-p <password>` above and you will be prompted for a password.

As an example of connecting as a user, we can login with the credentials of the user `john` that we created above by running the following command and entering the password when prompted:
<div class='copy separator-dollar'>
```sh
$ cqlsh -u john
```
</div>

As an example of connecting as the `admin` user, we can run the following command:
<div class='copy separator-dollar'>
```sh
$ cqlsh -u admin -p PasswdForAdmin
```
</div>

## 5. Edit user accounts

You can edit existing user accounts using the [ALTER ROLE](../../api/ycql/ddl_alter_role/) command. Note that the role making these changes should have sufficient privileges to modify the target role.

### Changing password for a user

To change the password for `john` above, you can do:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> ALTER ROLE john WITH PASSWORD = 'new-password';
```
</div>

### Granting and removing superuser privileges

In the example above, we can verify that `john` is not a superuser:

```{.sql}
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';

 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |        False |          []

(1 rows)
```


To grant superuser privileges to `john`, run the following command:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> ALTER ROLE john WITH SUPERUSER = true;
```
</div>
We can now verify that john is now a superuser:

```{.sql}
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';

 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |         True |          []

(1 rows)
```

Similarly, you can revoke superuser privileges by running:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> ALTER ROLE john WITH SUPERUSER = false;
```
</div>


### Enable and disable login privileges

In the example above, we can verify that `john` is can login to the database by doing the following:

```{.sql}
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';

 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |      True |        False |          []

(1 rows)
```

To disable login privileges for `john`, run the following command:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> ALTER ROLE john WITH LOGIN = false;
```
</div>

You can verify this as follows:

```{.sql}
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles WHERE role='john';

 role | can_login | is_superuser | member_of
------+-----------+--------------+-----------
 john |     False |        False |          []

(1 rows)
```

Trying to login as `john` using `cqlsh` will throw the following error:

```{.sh}
$ cqlsh -u john
Password:
Connection error:
  ... message="john is not permitted to log in"
```

To re-enable login privileges for `john`, run the following command:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh>  ALTER ROLE john WITH LOGIN = true;
```
</div>


## 6. Change default admin credentials

It is highly recommended to change at least the default password for the superadmin user in real world deployments to keep the database cluster secure.


As an example, let us say we want to change the `cassandra` user's password from `cassandra` to `new_password`. You can do that as follows:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> ALTER ROLE cassandra WITH PASSWORD = 'new_password';
```
</div>

Connecting to the cluster with the default password would no longer work:
```
$ bin/cqlsh -u cassandra -p cassandra
Connection error:
  ... Provided username cassandra and/or password are incorrect ...
```

You can now connect to the cluster using the new password:
<div class='copy separator-dollar'>
```sh
$ cqlsh -u cassandra -p new_password
```
</div>

## 7. Deleting a user

You can delete a user with the [DROP ROLE](../../api/ycql/ddl_drop_role/) command.

For example, to drop the user `john` in the above example, run the following command as a superuser:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> DROP ROLE IF EXISTS john;
```
</div>

You can verify that the `john` role was dropped as follows:

```{.sql}
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;

 role      | can_login | is_superuser | member_of
-----------+-----------+--------------+-----------
     admin |      True |         True |          []
 cassandra |      True |         True |          []

(2 rows)
```
