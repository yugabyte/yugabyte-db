
In this tutorial, we shall run through a scenario. Assume a company has an engineering organization, with three sub-teams - developers, qa and DB admins. We are going to create a role for each of these entities.

Here is what we want to achieve from a role-based access control (RBAC) perspective.

+ All members of engineering should be able to read data from any database and table.
+ Both developers and qa should be able to modify data in existing tables in the database `dev_database`.
+ QA should be able to alter the `integration_tests` table in the database `dev_database`.
+ DB admins should be able to perform all operations on any database.

## 1. Create role hierarchy

Connect to the cluster using a superuser role. Read more about [enabling authentication and connecting using a superuser role](../../ysql-authentication/) in YugabyteDB clusters for YSQL. For this tutorial, we are using the default `yugabyte` user and connect to the cluster using `ysqlsh` as follows:

```sh
$ ysqlsh -U yugabyte
```

Create a database `eng_database`.

```postgresql
yugabyte=# CREATE database dev_database;
```

Create the `dev_database.integration_tests` table:

```postgresql
CREATE TABLE dev_database.integration_tests (
  id UUID PRIMARY KEY,
  time TIMESTAMP,
  result BOOLEAN,
  details JSONB
);
```

Next, create roles `engineering`, `developer`, `qa` and `db_admin`.

```postgresql
yugabyte=# CREATE ROLE engineering;
                 CREATE ROLE developer;
                 CREATE ROLE qa;
                 CREATE ROLE db_admin;
```

Grant the `engineering` role to `developer`, `qa` and `db_admin` roles since they are all a part of the engineering organization.

```postgresql
yugabyte=# GRANT engineering TO developer;
                 GRANT engineering TO qa;
                 GRANT engineering TO db_admin;
```

List all the roles.

```postgresql
yugabyte=# SELECT rolname, rolcanlogin, rolsuperuser, memberof FROM pg_roles;
```

You should see the following output:

```
 rolname     | rolcanlogin | rolsuper | memberof
-------------+-------------+----------+-----------------
 qa          | f           | f        | {engineering}
 developer   | f           | f        | {engineering}
 engineering | f           | f        | {}
 db_admin    | f           | f        | {engineering}
 yugabyte    | t           | t        | {}

(5 rows)
```

## 2. List privileges for roles

You can list all privileges granted to the various roles with the following command:

```postgresql
yugabyte=# SELECT * FROM pg_privileges;
```

You should see something like the following output.

```
 role      | resource          | privileges
-----------+-------------------+--------------------------------------------------------------
 yugabyte  | roles/engineering |                               ['ALTER', 'AUTHORIZE', 'DROP']
 yugabyte  |   roles/developer |                               ['ALTER', 'AUTHORIZE', 'DROP']
 yugabyte  |          roles/qa |                               ['ALTER', 'AUTHORIZE', 'DROP']
 yugabyte  | data/dev_database | ['ALTER', 'AUTHORIZE', 'CREATE', 'DROP', 'MODIFY', 'SELECT']
 yugabyte  |    roles/db_admin |                               ['ALTER', 'AUTHORIZE', 'DROP']

(5 rows)
```

The above shows the various privileges the `yugabyte` role has. Since `yugabyte` is a superuser, it has all privileges on all DATABASES, including `ALTER`, `AUTHORIZE` and `DROP` on the roles we created (`engineering`, `developer`, `qa` and `db_admin`).

{{< note title="Note" >}}

For the sake of brevity, we will drop the `yugabyte` role-related entries in the remainder of this tutorial.

{{< /note >}}

## 3. Grant privileges to roles

In this section, we will grant privileges to achieve the following as mentioned in the beginning of this tutorial:

+ All members of engineering should be able to read data from any database and table.
+ Both developers and qa should be able to modify data in existing tables in the database `dev_database`.
+ Developers should be able to create, alter and drop tables in the database `dev_database`.
+ DB admins should be able to perform all operations on any database.

### Grant read access

All members of engineering should be able to read data from any database and table. Use the `GRANT SELECT` command to grant `SELECT` (or read) access on `ALL DATABASES` to the `engineering` role. This can be done as follows:

```postgresql
yugabyte=# GRANT SELECT ON ALL DATABASES TO engineering;
```

We can now verify that the `engineering` role has `SELECT` privilege as follows:

```postgresql
yugabyte=# SELECT * FROM system_auth.role_privileges;
```

The output should look similar to below, where we see that the `engineering` role has `SELECT` privilege on the `data` resource.

```
 role        | resource          | privileges
-------------+-------------------+--------------------------------------------------------------
 engineering |              data |                                                   ['SELECT']
 ...
```

{{< note title="Note" >}}

The resource `data` represents *all DATABASES and tables*.

{{< /note >}}

Granting the role `engineering` to any other role will cause all those roles to inherit the `SELECT` privileges. Thus, `developer`, `qa` and `db_admin` will all inherit the `SELECT` privilege.

### Grant data modify access

Both developers and qa should be able to modify data existing tables in the database `dev_database`. They should be able to execute statements such as `INSERT`, `UPDATE`, `DELETE` or `TRUNCATE` in order to modify data on existing tables. This can be done as follows:

```postgresql
yugabyte=# GRANT MODIFY ON database dev_database TO developer;
                 GRANT MODIFY ON database dev_database TO qa;
```

We can now verify that the `developer` and `qa` roles have the appropriate `MODIFY` privilege by running the following command.

```postgresql
yugabyte=# \l
```

We should see that the `developer` and `qa` roles have `MODIFY` privileges on the database `data/dev_database`.

```
 role        | resource          | Access privileges
-------------+-------------------+--------------------------------------------------------------
          qa | data/dev_database |                                                   ['MODIFY']
   developer | data/dev_database |                                                   ['MODIFY']
 engineering |              data |                                                   ['SELECT']
 ...
```

{{< note title="Note" >}}
In the resource hierarchy, `data` represents all DATABASES and `data/dev_database` represents one database in it.
{{< /note >}}

### Grant alter table access

QA should be able to alter the table `integration_tests` in the database `dev_database`. This can be done as follows.

```postgresql
yugabyte=# GRANT ALTER ON TABLE dev_database.integration_tests TO qa;
```

Once again, run the following command to verify the privileges.

```postgresql
yugabyte=# SELECT * FROM system_auth.role_privileges;
```

We should see a new row added, which grants the `ALTER` privilege on the resource `data/dev_database/integration_tests` to the role `qa`.

```
 role        | resource                            | privileges
-------------+-------------------------------------+--------------------------------------------------------------
          qa |                   data/dev_database |                                                   ['MODIFY']
          qa | data/dev_database/integration_tests |                                                    ['ALTER']
   developer |                   data/dev_database |                                                   ['MODIFY']
 engineering |                                data |                                                   ['SELECT']
```

{{< note title="Note" >}}

The resource `data/dev_database/integration_tests` denotes the hierarchy:

All DATABASES (`data`) > database (`dev_database`) > table (`integration_tests`)

{{< /note >}}

### Grant all privileges

DB admins should be able to perform all operations on any database. There are two ways to achieve this:

1. The DB admins can be granted the superuser privilege. Read more about [granting the superuser privilege to roles](../authentication/#YSQL). Note that doing this will give the DB admin all the privileges over all the roles as well.

2. Grant `ALL` privileges to the `db_admin` role. This can be achieved as follows.

```postgresql
yugabyte=# GRANT ALL ON ALL DATABASES TO db_admin;
```

Run the following command to verify the privileges:

```postgresql
yugabyte=# SELECT * FROM system_auth.role_privileges;
```

We should see the following, which grants the all privileges on the resource `data` to the role `db_admin`.

```
 role        | resource                            | privileges
-------------+-------------------------------------+--------------------------------------------------------------
          qa |                   data/dev_database |                                                   ['MODIFY']
          qa | data/dev_database/integration_tests |                                                    ['ALTER']
   developer |                   data/dev_database |                                                   ['MODIFY']
 engineering |                                data |                                                   ['SELECT']
    db_admin |                                data | ['ALTER', 'AUTHORIZE', 'CREATE', 'DROP', 'MODIFY', 'SELECT']
    ...
```

## 4. Revoke privileges from roles

Let us say we want to revoke the `AUTHORIZE` privilege from the DB admins so that they can no longer change privileges for other roles. This can be done as follows.

```postgresql
yugabyte=# REVOKE AUTHORIZE ON ALL DATABASES FROM db_admin;
```

Run the following command to verify the privileges.

```postgresql
yugabyte=# SELECT * FROM system_auth.role_privileges;
```

We should see the following output.

```
 role        | resource                            | privileges
-------------+-------------------------------------+--------------------------------------------------------------
          qa |                   data/dev_database |                                                   ['MODIFY']
          qa | data/dev_database/integration_tests |                                                    ['ALTER']
   developer |                   data/dev_database |                                                   ['MODIFY']
 engineering |                                data |                                                   ['SELECT']
    db_admin |                                data |              ['ALTER', 'CREATE', 'DROP', 'MODIFY', 'SELECT']
    ...
```

The `AUTHORIZE` privilege is no longer granted to the `db_admin` role.
