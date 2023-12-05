---
title: Rate limiting connections
headerTitle: Rate limiting connections
linkTitle: Rate limiting connections
description: Rate limiting connections in YugabyteDB.
headcontent: Rate limiting connections in YugabyteDB.
image: <div class="icon"><i class="fa-solid fa-file-invoice-dollar"></i></div>
menu:
  stable:
    name: Rate limiting connections
    identifier: develop-quality-of-service-limiting-connections
    parent: develop-quality-of-service
    weight: 210
type: docs
---

Each connection to a YugabyteDB cluster uses CPU and memory, so it is important to consider how many connections are needed for the application. YugabyteDB uses a `max_connections` setting to limit the number of connections per node in the cluster (and thereby the resources that are consumed by connections) to prevent run-away connection behavior from overwhelming your deployment's resources.

You can check the value of `max_connections` with your admin user and `ysqlsh`.

```sql
SHOW max_connections;
```

```output
 max_connections
-----------------
 300
(1 row)
```

{{< note title="Note" >}}
YugabyteDB reports max_connections differently depending on your user role: non-superuser roles see only the connections available for use, while superusers see all connections, including those reserved for superusers.
{{< /note >}}

## Limiting connections per tenant

Sometimes it is important to limit the number of connections per tenant. In order to achieve this, map a tenant to a database and a user (or a service account), and rate limit the number of connections per database for the user. This can be achieved as shown in this section.

{{< note title="Note" >}}
The connection limit is stored in the system catalog but connection count tracking is per node and kept in shared memory. For example, if the connection limit is set to 1, then on 3 node clusters, it is possible to launch 3 connections when there is one connection on each node.
{{< /note >}}

### Set up database and user

  1. First create a database, as follows:

     ```sql
     create database test_connection;
     ```

  1. Next, create a user. Make sure the user you create is not a superuser. For superusers, there is no limit on the number of connections.

     ```sql
     create role test_user login;
     ```

  1. Verify that you created a non-superuser using the following command:

     ```sql
     SELECT rolname, rolsuper, rolcanlogin FROM pg_roles;
     ```

     You should see the following output.

     ```output
      rolname                   | rolsuper | rolcanlogin
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
      yb_db_admin               | f        | f
      yugabyte                  | t        | t
      test_user                 | f        | t
     ```

### Limit connections per DB

- Set the database connection limit:

  ```sql
  alter database test_connection CONNECTION LIMIT 1;
  ```

- You can display the limits as follows:

  ```sql
  select datname, datconnlimit from pg_database where datname ='test_connection' ;
  ```

  ```output
         datname     | datconnlimit
  -----------------+--------------
   test_connection |            1
  ```

### Test connection limit

To test, launch two connections to the database.

The first connection should succeed.

```sql
./bin/ysqlsh -U test_user test_connection
```

The second connection should fail.

```sql
./bin/ysqlsh -U test_user test_connection
```

```output
ysqlsh: FATAL:  too many connections for database "test_connection"
```

## Learn more

- [YSQL Connection Manager](../../../explore/connection-manager/connection-mgr-ysql/)
