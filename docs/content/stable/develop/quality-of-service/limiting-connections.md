---
title: Rate limiting connections
headerTitle: Rate limiting connections
linkTitle: Rate limiting connections
description: Rate limiting connections in YugabyteDB.
headcontent: Rate limiting connections in YugabyteDB.
image: <div class="icon"><i class="fas fa-file-invoice-dollar"></i></div>
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

```
SHOW max_connections;
 max_connections
-----------------
 300
(1 row)
```

## Limiting connections per tenant

Sometimes it is important to limit the number of connections per tenant. In order to achieve this, map a tenant to a database and a user (or a service account), and rate limit the number of connections per database for the user. This can be achieved as shown in this section.

{{< note title="Note" >}}
The connection limit is stored in the System catalog but connection count tracking is per node and kept in shared memory.  E.g if the connection limit is set to 1  then  on 3 Node clusters, it is possible to launch 3 connections when there is one connection on each node.
{{< /note >}}

### Setup database and user

First create a database, as shown below.
```sql
create database test_connection;
```

Next, create a user. Make sure the user you create is not a superuser. For superusers, there is no limit on the number of connections.
```sql
create role test_user login;
```

### Limit connections per DB
Set connection limit for database as shown below.
```sql
alter database test_connection CONNECTION LIMIT 1;
```

You can display the limits as shown below.
```sql
select datname, datconnlimit from pg_database where datname =’test_connection’ ;
```
```
       datname     | datconnlimit
-----------------+--------------
 test_connection |            1
```

### Test connection limit
To test, launch two connections to the database.

The first connection should succeed.
```
./bin/ysqlsh -U test_user test_connection
This is successful
```

The second connection should fail.
```
./bin/ysqlsh -U test_user test_connection
ysqlsh: FATAL:  too many connections for database "test_connection"
```
