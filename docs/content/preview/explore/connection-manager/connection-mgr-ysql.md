---
title: Connection Manager in YSQL
headerTitle: YSQL Connection Manager
linkTitle: YSQL Connection Manager
description: Built-in server-side connection pooler for YSQL
headcontent: Built-in server-side connection pooler for YSQL
techPreview: /preview/releases/versioning/#feature-availability
menu:
  preview:
    name: YSQL Connection Manager
    identifier: explore-conn-mgr-ysql
    parent: explore
    weight: 800
type: docs
---

YugabyteDB inherits the architecture of creating one backend process for every connection to the database from PostgreSQL. These backend processes consume memory and CPU, limiting the number of connections YugabyteDB can support. To solve this problem, you can use a connection pooler, which allows multiplexing multiple client connections to a smaller number of actual server connections, thereby supporting a larger number of connections from applications. [PgBouncer](https://github.com/pgbouncer/pgbouncer) and [Odyssey](https://github.com/yandex/odyssey) are some of the popular PostgreSQL-based server-side connection pooling mechanisms which are fully compatible with YugabyteDB.

However, these products have some severe limitations such as the following:

- PgBouncer does not support prepared statements in the transaction pooling mode.
- Both Odyssey and PgBouncer do not support SET statements in the transaction pooling mode.

YugabyteDB includes a built-in connection pooler, YSQL Connection Manager, which provides the same connection pooling advantages as other pooling solutions but without these limitations. As the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections.

{{< note title = "Note">}}
YSQL Connection Manager is currently not supported for [YugabyteDB Anywhere](../../../yugabyte-platform/) and [YugabyteDB Managed](../../../yugabyte-cloud/).
{{< /note >}}

![Connection manager](/images/explore/connection-manager.png)

## Key features

YSQL Connection Manager is a modified version of the open source connection pooler Odyssey. YSQL Connection Manager uses Odyssey in the transaction pooling mode, and has been modified at the wire protocol level for tighter integration with YugabyteDB to overcome some SQL limitations.

YSQL Connection Manager has the following key features:

- No SQL limitations - Unlike other pooling solutions running in transaction mode, YSQL Connection Manager supports SQL features such as TEMP TABLE, WITH HOLD CURSORS, and more.

- Single pool per database - PgBouncer and Odyssey create a pool for every combination of users and databases, which significantly limits the number of users that can be supported and therefore impacts scalability. YSQL Connection Manager, however, creates one pool per database - all connections trying to access the same database share the same single pool meant for that database.

- Support for session parameters - YSQL Connection Manager supports SET statements, which are not supported by other connection poolers.

- Support for prepared statements - Odyssey supports protocol-level prepared statements and YSQL Connection Manager inherits this feature.

## Use YSQL Connection Manager

To start a YugabtyeDB cluster with YSQL Connection Manager, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `enable_ysql_conn_mgr` flag to true.

When `enable_ysql_conn_mgr` is set, each YB-TServer starts the YSQL Connection Manager process along with the PostgreSQL process. You should see one YSQL Connection Manager process per YB-TServer.

To create a single-node cluster with YSQL Connection Manager using [yugabyted](../../../reference/configuration/yugabyted/), use the following  command:

```sh
./bin/yugabyted start --tserver_flags "enable_ysql_conn_mgr=true,allowed_preview_flags_csv=enable_ysql_conn_mgr" --ui false
```

Because `enable_ysql_conn_mgr` is a preview flag only, to use it, add the flag to the `allowed_preview_flags_csv` list (that is, `allowed_preview_flags_csv=enable_ysql_conn_mgr`).

{{< note >}}

To create a large number of client connections, ensure that "SHMMNI" (the maximum number of concurrent shared memory segments an OS allows) as well as [ulimit](../../../deploy/manual-deployment/system-config/#ulimits) is set correctly as follows:

1. Open the file `/etc/sysctl.conf`.
1. Add `kernel.shmmni = 32768` (support for 30000 clients) at the end of the file.
1. To refresh the settings, use `sudo sysctl -p`.
{{< /note >}}

### YSQL Connection Manager ports and flags

By default, when YSQL Connection Manager is enabled, it uses the port 5433, and the backend database is assigned a random free port.

To explicitly set a port for YSQL, you should specify ports for the flags `ysql_conn_mgr_port` and [ysql_port](../../../reference/configuration/yugabyted/#advanced-flags).

The following table describes YB-TServer flags related to YSQL Connection Manager:

| flag | Description | Default |
|:---- | :---------- | :------ |
| enable_ysql_conn_mgr | Enables YSQL Connection Manager for the cluster. YB-TServer starts a YSQL Connection Manager process as a child process. | false |
| ysql_conn_mgr_idle_time | Specifies the maximum idle time (in seconds) allowed for database connections created by YSQL Connection Manager. If a database connection remains idle without serving a client connection for a duration equal to, or exceeding this value, it is automatically closed by YSQL Connection Manager. | 60 |
| ysql_conn_mgr_max_client_connections | Maximum number of concurrent database connections YSQL Connection Manager can create per pool. | 10000 |
| ysql_conn_mgr_min_conns_per_db | Minimum number of physical connections that is present in the pool. This limit is not considered while closing a broken physical connection. | 1 |
| ysql_conn_mgr_num_workers | Number of worker threads used by YSQL Connection Manager. If set to 0, the number of worker threads will be half of the number of CPU cores. | 0 |
| ysql_conn_mgr_stats_interval | Interval (in seconds) for updating the YSQL Connection Manager statistics. | 10 |
| ysql_conn_mgr_password | Password to be used by YSQL Connection Manager for creating database connections. | yugabyte |
| ysql_conn_mgr_username | Username to be used by YSQL Connection Manager for creating database connections.| yugabyte |
| ysql_conn_mgr_warmup_db | Database for which warmup needs to be done. | yugabyte |
| enable_ysql_conn_mgr_stats | Enable statistics collection from YSQL Connection Manager. These statistics are displayed at the endpoint `<ip_address_of_cluster>:13000/connections`. | true |
| ysql_conn_mgr_port | YSQL Connection Manager port to which clients can connect. This must be different from the PostreSQL port set via `pgsql_proxy_bind_address`. | 5433 |
| ysql_conn_mgr_dowarmup | Enable pre-creation of server connections in YSQL Connection Manager. If set to false, the server connections are created lazily (on-demand) in YSQL Connection Manager. | false |
