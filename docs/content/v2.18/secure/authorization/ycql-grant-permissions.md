---
title: Grant YCQL privileges in YugabyteDB
headerTitle: Grant privileges
linkTitle: Grant privileges
description: Grant YCQL privileges in YugabyteDB
menu:
  v2.18:
    name: Grant privileges
    identifier: ycql-grant-permissions
    parent: authorization
    weight: 736
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql-grant-permissions/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../ycql-grant-permissions/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

This tutorial demonstrates how to grant privileges in YCQL using the scenario of a company with an engineering organization that has three sub-teams: developers, QA, and DB admins.

Here is what you want to achieve from a role-based access control (RBAC) perspective:

- All members of engineering should be able to read data from any keyspace and table.
- Developers and QA should be able to modify data in existing tables in the keyspace `dev_keyspace`.
- QA should be able to alter the `integration_tests` table in the keyspace `dev_keyspace`.
- DB admins should be able to perform all operations on any keyspace.

The exercise assumes you have [enabled authentication for YCQL](../../enable-authentication/ycql/).

## 1. Create role hierarchy

Connect to the cluster using a superuser role. Use the default `cassandra` user and connect to the cluster using `ycqlsh` as follows:

```sh
$ ./bin/ycqlsh -u cassandra -p cassandra
```

Create a keyspace `dev_keyspace`.

```cql
cassandra@ycqlsh> CREATE KEYSPACE IF NOT EXISTS dev_keyspace;
```

Create the `dev_keyspace.integration_tests` table:

```cql
CREATE TABLE dev_keyspace.integration_tests (
  id UUID PRIMARY KEY,
  time TIMESTAMP,
  result BOOLEAN,
  details JSONB
);
```

Next, create roles `engineering`, `developer`, `qa`, and `db_admin`.

```cql
cassandra@ycqlsh> CREATE ROLE IF NOT EXISTS engineering;
                 CREATE ROLE IF NOT EXISTS developer;
                 CREATE ROLE IF NOT EXISTS qa;
                 CREATE ROLE IF NOT EXISTS db_admin;
```

Grant the `engineering` role to `developer`, `qa`, and `db_admin` roles, as they are all a part of the engineering organization.

```cql
cassandra@ycqlsh> GRANT engineering TO developer;
                 GRANT engineering TO qa;
                 GRANT engineering TO db_admin;
```

List all the roles.

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

You should see the following output:

```output
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

```cql
cassandra@ycqlsh> SELECT * FROM system_auth.role_permissions;
```

You should see something like the following output:

```output
 role      | resource          | permissions
-----------+-------------------+--------------------------------------------------------------
 cassandra | roles/engineering |                               ['ALTER', 'AUTHORIZE', 'DROP']
 cassandra |   roles/developer |                               ['ALTER', 'AUTHORIZE', 'DROP']
 cassandra |          roles/qa |                               ['ALTER', 'AUTHORIZE', 'DROP']
 cassandra | data/dev_keyspace | ['ALTER', 'AUTHORIZE', 'CREATE', 'DROP', 'MODIFY', 'SELECT']
 cassandra |    roles/db_admin |                               ['ALTER', 'AUTHORIZE', 'DROP']

(5 rows)
```

This shows the various permissions the `cassandra` role has. Because `cassandra` is a superuser, it has all permissions on all keyspaces, including `ALTER`, `AUTHORIZE`, and `DROP` on the roles you created (`engineering`, `developer`, `qa`, and `db_admin`).

{{< note title="Note" >}}

For the sake of brevity, the `cassandra` role related entries are not included in the remainder of this article.

{{< /note >}}

## 3. Grant permissions to roles

In this section, you grant permissions to each role.

### Grant read access

All members of engineering need to be able to read data from any keyspace and table. Use the `GRANT SELECT` command to grant `SELECT` (or read) access on `ALL KEYSPACES` to the `engineering` role. This can be done as follows:

```cql
cassandra@ycqlsh> GRANT SELECT ON ALL KEYSPACES TO engineering;
```

Verify that the `engineering` role has `SELECT` permission as follows:

```cql
cassandra@ycqlsh> SELECT * FROM system_auth.role_permissions;
```

The output should look similar to below, where the `engineering` role has `SELECT` permission on the `data` resource.

```output
 role        | resource          | permissions
-------------+-------------------+--------------------------------------------------------------
 engineering |              data |                                                   ['SELECT']
 ...
```

{{< note title="Note" >}}

The resource "data" represents *all keyspaces and tables*.

{{< /note >}}

Granting the role `engineering` to any other role causes all those roles to inherit the `SELECT` permissions. Thus, `developer`, `qa`, and `db_admin` all inherit the `SELECT` permission.

### Grant data modify access

Developers and QA should be able to modify data in existing tables in the keyspace `dev_keyspace`. They should be able to execute statements such as `INSERT`, `UPDATE`, `DELETE`, or `TRUNCATE` to modify data on existing tables. This can be done as follows:

```cql
cassandra@ycqlsh> GRANT MODIFY ON KEYSPACE dev_keyspace TO developer;
                 GRANT MODIFY ON KEYSPACE dev_keyspace TO qa;
```

Verify that the `developer` and `qa` roles have the appropriate `MODIFY` permission by running the following command.

```cql
cassandra@ycqlsh> SELECT * FROM system_auth.role_permissions;
```

The `developer` and `qa` roles have `MODIFY` permissions on the keyspace `data/dev_keyspace`.

```output
 role        | resource          | permissions
-------------+-------------------+--------------------------------------------------------------
          qa | data/dev_keyspace |                                                   ['MODIFY']
   developer | data/dev_keyspace |                                                   ['MODIFY']
 engineering |              data |                                                   ['SELECT']
 ...
```

{{< note title="Note" >}}

In the resource hierarchy, "data" represents all keyspaces and "data/dev_keyspace" represents one keyspace in it.

{{< /note >}}

### Grant alter table access

QA should be able to alter the table `integration_tests` in the keyspace `dev_keyspace`. This can be done as follows.

```cql
cassandra@ycqlsh> GRANT ALTER ON TABLE dev_keyspace.integration_tests TO qa;
```

Run the following command to verify the permissions.

```cql
cassandra@ycqlsh> SELECT * FROM system_auth.role_permissions;
```

The `ALTER` permission on the resource `data/dev_keyspace/integration_tests` is granted to the role `qa`.

```output
 role        | resource                            | permissions
-------------+-------------------------------------+--------------------------------------------------------------
          qa |                   data/dev_keyspace |                                                   ['MODIFY']
          qa | data/dev_keyspace/integration_tests |                                                    ['ALTER']
   developer |                   data/dev_keyspace |                                                   ['MODIFY']
 engineering |                                data |                                                   ['SELECT']
```

{{< note title="Note" >}}

The resource "data/dev_keyspace/integration_tests" denotes the hierarchy:

All Keyspaces (data) > keyspace (dev_keyspace) > table (integration_tests)

{{< /note >}}

### Grant all permissions

DB admins should be able to perform all operations on any keyspace. There are two ways to achieve this:

1. Grant DB admins the [superuser](../../enable-authentication/ycql/#create-a-superuser) permission. Doing this gives DB admins all permissions over all roles as well.

2. Grant ALL permissions to the "db_admin" role. Do the following:

    ```cql
    cassandra@ycqlsh> GRANT ALL ON ALL KEYSPACES TO db_admin;
    ```

    Run the following command to verify the permissions:

    ```cql
    cassandra@ycqlsh> SELECT * FROM system_auth.role_permissions;
    ```

    All permissions on the resource `data` are granted to the role `db_admin`.

    ```output
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

To revoke the `AUTHORIZE` permission from DB admins so that they can no longer change permissions for other roles, do the following:

```cql
cassandra@ycqlsh> REVOKE AUTHORIZE ON ALL KEYSPACES FROM db_admin;
```

Run the following command to verify the permissions.

```cql
cassandra@ycqlsh> SELECT * FROM system_auth.role_permissions;
```

You should see the following output.

```output
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
