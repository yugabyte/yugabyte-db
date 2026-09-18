---
title: postgres_fdw extension
headerTitle: postgres_fdw extension
linkTitle: postgres_fdw
description: Using the postgres_fdw extension in YugabyteDB
menu:
  stable:
    identifier: extension-postgres-fdw
    parent: pg-extensions
    weight: 20
type: docs
aliases:
  - /stable/explore/ysql-language-features/pg-extensions/extension-postgres-fdw
---

The [postgres_fdw](https://www.postgresql.org/docs/15/postgres-fdw.html) module provides the foreign-data wrapper postgres_fdw, which can be used to access data stored in external PostgreSQL or YugabyteDB servers.

In v2026.1.2 and later, the extension is installed into `pg_catalog` during cluster initialization (and on upgrade) so [global views](../../../explore/observability/global-views/) can be created by default. `CREATE EXTENSION postgres_fdw` is still harmless if you run it:

```plpgsql
CREATE EXTENSION postgres_fdw;
```

To connect to a remote YSQL or PostgreSQL database, create a foreign server object. Specify the connection information (except the username and password) using the `OPTIONS` clause. Include [`server_type`](#server-type-option) when the remote is YugabyteDB:

```plpgsql
CREATE SERVER my_server FOREIGN DATA WRAPPER postgres_fdw
    OPTIONS (host 'host_ip', dbname 'external_db', port 'port_number');
```

Specify the username and password using `CREATE USER MAPPING`:

```plpgsql
CREATE USER MAPPING FOR mylocaluser SERVER my_server OPTIONS (user 'remote_user', password 'password');
```

You can now create foreign tables using `CREATE FOREIGN TABLE` and `IMPORT FOREIGN SCHEMA`:

```plpgsql
CREATE FOREIGN TABLE table_name (colname1 int, colname2 int) SERVER my_server OPTIONS (schema_name 'schema', table_name 'table');
IMPORT FOREIGN SCHEMA foreign_schema_name FROM SERVER my_server INTO local_schema_name;
```

You can execute `SELECT` statements on the foreign tables to access the data in the corresponding remote tables.

## server_type option

`server_type` is a YugabyteDB addition to the server options `postgres_fdw` accepts. It tells the wrapper what kind of server is behind the foreign tables, which decides how rows are identified and, for one value, how the query runs.

| Value | Meaning |
| :---- | :------ |
| `postgreSQL` | The remote server is PostgreSQL. Rows are identified by `ctid`. Used when the option is omitted. |
| `yugabyteDB` | The remote server is another YugabyteDB cluster. Rows are identified by `ybctid`. |
| `federatedYugabyteDB` | There is no remote server. Each foreign table is read from every node of the local cluster. |

Set it when you create the server. For another YugabyteDB cluster:

```plpgsql
CREATE SERVER my_server FOREIGN DATA WRAPPER postgres_fdw
    OPTIONS (server_type 'yugabyteDB', host 'host_ip', dbname 'external_db', port 'port_number');
```

The value is case-insensitive. Leaving it out selects `postgreSQL` and reports it:

```output
NOTICE:  no server_type specified. Defaulting to PostgreSQL.
HINT:  Use "ALTER SERVER ... OPTIONS (ADD server_type '<type>')" to explicitly set server_type.
```

`federatedYugabyteDB` works differently from the other two. The server takes no `host`, `dbname`, or `port`, and needs no `CREATE USER MAPPING`, because the target nodes come from the cluster's own tablet server list and each node is reached over an internal connection. YugabyteDB creates one such server, `yb_global_views_server`, and uses it for [global views](../../../explore/observability/global-views/). You don't need to create this server yourself; it is equivalent to:

```plpgsql
CREATE SERVER yb_global_views_server FOREIGN DATA WRAPPER postgres_fdw
    OPTIONS (server_type 'federatedYugabyteDB');
```

{{<lead link="../../../explore/observability/global-views">}}
To query per-node statistics views across every live YB-TServer from a single session, see [Global views](../../../explore/observability/global-views).
{{</lead>}}
