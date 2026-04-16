---
title: postgres_fdw extension
headerTitle: postgres_fdw extension
linkTitle: postgres_fdw
description: Using the postgres_fdw extension in YugabyteDB
menu:
  v2.20:
    identifier: extension-postgres-fdw
    parent: pg-extensions
    weight: 20
type: docs
---

The [postgres_fdw](https://www.postgresql.org/docs/11/postgres-fdw.html) module provides the foreign-data wrapper postgres_fdw, which can be used to access data stored in external PostgreSQL servers.

First, enable the extension:

```sql
CREATE EXTENSION postgres_fdw;
```

To connect to a remote YSQL or PostgreSQL database, create a foreign server object. Specify the connection information (except the username and password) using the `OPTIONS` clause:

```sql
CREATE SERVER my_server FOREIGN DATA WRAPPER postgres_fdw OPTIONS (host 'host_ip', dbname 'external_db', port 'port_number');
```

Specify the username and password using `CREATE USER MAPPING`:

```sql
CREATE USER MAPPING FOR mylocaluser SERVER my_server OPTIONS (user 'remote_user', password 'password');
```

You can now create foreign tables using `CREATE FOREIGN TABLE` and `IMPORT FOREIGN SCHEMA`:

```sql
CREATE FOREIGN TABLE table_name (colname1 int, colname2 int) SERVER my_server OPTIONS (schema_name 'schema', table_name 'table');
IMPORT FOREIGN SCHEMA foreign_schema_name FROM SERVER my_server INTO local_schema_name;
```

You can execute `SELECT` statements on the foreign tables to access the data in the corresponding remote tables.
