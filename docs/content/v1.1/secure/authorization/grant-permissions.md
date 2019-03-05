---
title: 3. Grant Permissions
linkTitle: 3. Grant Permissions
description: 3. Grant Permissions
headcontent: Grant Permissions in YCQL
image: /images/section_icons/secure/grant-permissions.png
aliases:
  - /secure/authorization/grant-permissions/
menu:
  v1.1:
    identifier: secure-authorization-grant-permissions
    parent: secure-authorization
    weight: 718
isTocNested: true
showAsideToc: true
---

In this tutorial, we shall run through a scenario. Assume a company has an engineering organization, with three sub-teams - developers, qa and DB admins. We are going to create a role for each of these entities.

Here is what we want to achieve from an RBAC perspective:

+ All members of engineering should be able to read data from any keyspace and table.
+ Both developers and qa should be able to modify data in existing tables in the keyspace `dev_keyspace`.
+ QA should be able to alter the `integration_tests` table in the keyspace `dev_keyspace`.
+ DB admins should be able to perform all operations on any keyspace.


## 1. Create role hierarchy

Connect to the cluster using a superuser role. Read more about [enabling authentication and connecting using a superuser role](../../authentication/#ycql) in YugaByte DB clusters for YCQL. For this article, we are using the default `cassandra` user and connect to the cluster using `cqlsh` as follows:

```sh
$ cqlsh -u cassandra -p cassandra
```

Create a keyspace `eng_keyspace`.

```sql
cassandra@cqlsh> CREATE KEYSPACE IF NOT EXISTS dev_keyspace;
```

Create the `dev_keyspace.integration_tests` table:

```
CREATE TABLE dev_keyspace.integration_tests (
  id UUID PRIMARY KEY,
  time TIMESTAMP,
  result BOOLEAN,
  details JSONB
);
```

Next, create roles `engineering`, `developer`, `qa` and `db_admin`.

```sql
cassandra@cqlsh> CREATE ROLE IF NOT EXISTS engineering;
                 CREATE ROLE IF NOT EXISTS developer;
                 CREATE ROLE IF NOT EXISTS qa;
                 CREATE ROLE IF NOT EXISTS db_admin;
```

Grant the `engineering` role to `developer`, `qa` and `db_admin` roles since they are all a part of the engineering organization.

```sql
cassandra@cqlsh> GRANT engineering TO developer;
                 GRANT engineering TO qa;
                 GRANT engineering TO db_admin;
```

List all the roles.

```sql
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

You should see the following output:

```
 role        | can_login | is_superuser | member_of
-------------+-----------+--------------+-----------------
          qa |     False |        False | ['engineering']
   developer |     False |        False | ['engineering']
 engineering |     False |        False |                []
    db_admin |     False |        False | ['engineering']
   cassandra |      True |         True |                []

(5 rows)
```


## 2. List permissions for roles

You can list all permissions granted to the various roles with the following command:

```sql
cassandra@cqlsh> SELECT * FROM system_auth.role_permissions;
```

You should see something like the following output:

```
 role      | resource          | permissions
-----------+-------------------+--------------------------------------------------------------
 cassandra | roles/engineering |                               ['ALTER', 'AUTHORIZE', 'DROP']
 cassandra |   roles/developer |                               ['ALTER', 'AUTHORIZE', 'DROP']
 cassandra |          roles/qa |                               ['ALTER', 'AUTHORIZE', 'DROP']
 cassandra | data/dev_keyspace | ['ALTER', 'AUTHORIZE', 'CREATE', 'DROP', 'MODIFY', 'SELECT']
 cassandra |    roles/db_admin |                               ['ALTER', 'AUTHORIZE', 'DROP']

(5 rows)
```

The above shows the various permissions the `cassandra` role has. Since `cassandra` is a superuser, it has all permissions on all keyspaces, including `ALTER`, `AUTHORIZE` and `DROP` on the roles we created (`engineering`, `developer`, `qa` and `db_admin`).

{{< note title="Note" >}}
For the sake of brevity, we will drop the `cassandra` role related entries in the remainder of this article.
{{< /note >}}


## 3. Grant permissions to roles

In this section, we will grant permissions to achieve the following as mentioned in the beginning of this tutorial:

+ All members of engineering should be able to read data from any keyspace and table.
+ Both developers and qa should be able to modify data in existing tables in the keyspace `dev_keyspace`.
+ Developers should be able to create, alter and drop tables in the keyspace `dev_keyspace`.
+ DB admins should be able to perform all operations on any keyspace.


### Grant read access

All members of engineering should be able to read data from any keyspace and table. Use the `GRANT SELECT` command to grant `SELECT` (or read) access on `ALL KEYSPACES` to the `engineering` role. This can be done as follows:

```sql
cassandra@cqlsh> GRANT SELECT ON ALL KEYSPACES TO engineering;
```

We can now verify that the `engineering` role has `SELECT` permission as follows:

```sql
cassandra@cqlsh> SELECT * FROM system_auth.role_permissions;
```

The output should look similar to below, where we see that the `engineering` role has `SELECT` permission on the `data` resource.


```
 role        | resource          | permissions
-------------+-------------------+--------------------------------------------------------------
 engineering |              data |                                                   ['SELECT']
 ...
```

{{< note title="Note" >}}
The resource `data` represents *all keyspaces and tables*.
{{< /note >}}

Granting the role `engineering` to any other role will cause all those roles to inherit the `SELECT` permissions. Thus, `developer`, `qa` and `db_admin` will all inherit the `SELECT` permission.


### Grant data modify access

Both developers and qa should be able to modify data existing tables in the keyspace `dev_keyspace`. They should be able to execute statements such as `INSERT`, `UPDATE`, `DELETE` or `TRUNCATE` in order to modify data on existing tables. This can be done as follows:

```sql
cassandra@cqlsh> GRANT MODIFY ON KEYSPACE dev_keyspace TO developer;
                 GRANT MODIFY ON KEYSPACE dev_keyspace TO qa;
```

We can now verify that the `developer` and `qa` roles have the appropriate `MODIFY` permission by running the following command:
```sql
cassandra@cqlsh> SELECT * FROM system_auth.role_permissions;
```

We should see that the `developer` and `qa` roles have `MODIFY` permissions on the keyspace `data/dev_keyspace`.

```
 role        | resource          | permissions
-------------+-------------------+--------------------------------------------------------------
          qa | data/dev_keyspace |                                                   ['MODIFY']
   developer | data/dev_keyspace |                                                   ['MODIFY']
 engineering |              data |                                                   ['SELECT']
 ...
```
{{< note title="Note" >}}
In the resource hierarchy, `data` represents all keyspaces and `data/dev_keyspace` represents one keyspace in it.
{{< /note >}}


### Grant alter table access

QA should be able to alter the table `integration_tests` in the keyspace `dev_keyspace`. This can be done as follows:
```sql
cassandra@cqlsh> GRANT ALTER ON TABLE dev_keyspace.integration_tests TO qa;
```

Once again, run the following command to verify the permissions:
```sql
cassandra@cqlsh> SELECT * FROM system_auth.role_permissions;
```

We should see a new row added, which grants the `ALTER` permission on the resource `data/dev_keyspace/integration_tests` to the role `qa`. 

```
 role        | resource                            | permissions
-------------+-------------------------------------+--------------------------------------------------------------
          qa |                   data/dev_keyspace |                                                   ['MODIFY']
          qa | data/dev_keyspace/integration_tests |                                                    ['ALTER']
   developer |                   data/dev_keyspace |                                                   ['MODIFY']
 engineering |                                data |                                                   ['SELECT']
```
{{< note title="Note" >}}
The resource `data/dev_keyspace/integration_tests` denotes the hierarchy:

All Keyspaces (`data`) > keyspace (`dev_keyspace`) > table (`integration_tests`)
{{< /note >}}


### Grant all privileges

DB admins should be able to perform all operations on any keyspace. There are two ways to achieve this:

1. The DB admins can be granted the superuser privilege. Read more about [granting the superuser privilege to roles](../authentication/#ycql). Note that doing this will give the DB admin all the permissions over all the roles as well.

2. Grant `ALL` privileges to the `db_admin` role. This can be achieved as follows:
```sql
cassandra@cqlsh> GRANT ALL ON ALL KEYSPACES TO db_admin;
```

Run the following command to verify the permissions:
```sql
cassandra@cqlsh> SELECT * FROM system_auth.role_permissions;
```

We should see the following, which grants the all permissions on the resource `data` to the role `db_admin`. 

```
 role        | resource                            | permissions
-------------+-------------------------------------+--------------------------------------------------------------
          qa |                   data/dev_keyspace |                                                   ['MODIFY']
          qa | data/dev_keyspace/integration_tests |                                                    ['ALTER']
   developer |                   data/dev_keyspace |                                                   ['MODIFY']
 engineering |                                data |                                                   ['SELECT']
    db_admin |                                data | ['ALTER', 'AUTHORIZE', 'CREATE', 'DROP', 'MODIFY', 'SELECT']
    ...
```


## 4. Revoke permissions from roles

Let us say we want to revoke the `AUTHORIZE` permission from the DB admins so that they can no longer change permissions for other roles. This can be done as follows:
```sql
cassandra@cqlsh> REVOKE AUTHORIZE ON ALL KEYSPACES FROM db_admin;
```

Run the following command to verify the permissions:
```sql
cassandra@cqlsh> SELECT * FROM system_auth.role_permissions;
```

We should see the following output: 

```
 role        | resource                            | permissions
-------------+-------------------------------------+--------------------------------------------------------------
          qa |                   data/dev_keyspace |                                                   ['MODIFY']
          qa | data/dev_keyspace/integration_tests |                                                    ['ALTER']
   developer |                   data/dev_keyspace |                                                   ['MODIFY']
 engineering |                                data |                                                   ['SELECT']
    db_admin |                                data |              ['ALTER', 'CREATE', 'DROP', 'MODIFY', 'SELECT']
    ...
```

The `AUTHORIZE` permission is no longer granted to the `db_admin` role.
