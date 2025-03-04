---
title: Connection Manager in YSQL
headerTitle: Built-in connection pooling
linkTitle: Built-in connection pooling
description: Built-in server-side connection pooler for YSQL
headcontent: Built-in server-side connection pooler for YSQL
tags:
  feature: early-access
aliases:
   - /preview/explore/connection-manager/connection-mgr-ysql/
   - /preview/explore/connection-manager/
menu:
  preview:
    name: Built-in connection pooling
    identifier: explore-conn-mgr-ysql
    parent: going-beyond-sql
    weight: 600
type: docs
---

YugabyteDB inherits the architecture of creating one backend process for every connection to the database from PostgreSQL. These backend processes consume memory and CPU, limiting the number of connections YugabyteDB can support. To solve this problem, you can use a connection pooler, which allows multiplexing multiple client connections to a smaller number of actual server connections, thereby supporting a larger number of connections from applications. [PgBouncer](https://github.com/pgbouncer/pgbouncer) and [Odyssey](https://github.com/yandex/odyssey) are some of the popular PostgreSQL-based server-side connection pooling mechanisms that are fully compatible with YugabyteDB.

However, connection poolers have some limitations:

- Added complexity. Deploying and maintaining a connection pooler adds complexity to your application stack.
- They don't support all PostgreSQL features. For example, neither PgBouncer nor Odyssey supports SET statements in the transaction pooling mode.

To provide the advantages of connection pooling, but without the limitations, YugabyteDB includes a built-in connection pooler, YSQL Connection Manager. Because the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections without additional third-party tools. When combined with [smart drivers](../../../drivers-orms/smart-drivers/), YugabyteDB simplifies application architecture and enhances developer productivity.

![Connection manager](/images/explore/ysql-connection-manager.png)

{{< note title = "Note">}}
YSQL Connection Manager is currently not supported for [YugabyteDB Aeon](../../../yugabyte-cloud/).
{{< /note >}}

## Key features

YSQL Connection Manager is a modified version of the open-source connection pooler Odyssey. YSQL Connection Manager uses Odyssey in the transaction pooling mode and has been modified at the wire protocol level for tighter integration with YugabyteDB to overcome some SQL limitations.

YSQL Connection Manager has the following key features:

- No SQL limitations - Unlike other pooling solutions running in transaction mode, YSQL Connection Manager supports SQL features such as TEMP TABLE, WITH HOLD CURSORS, and more.

- Single pool per database - PgBouncer and Odyssey create a pool for every combination of users and databases, which significantly limits the number of users that can be supported and therefore impacts scalability. YSQL Connection Manager, however, creates one pool per database - all connections trying to access the same database share the same single pool meant for that database.

- Support for session parameters - YSQL Connection Manager supports SET statements, which are not supported by other connection poolers.

- Support for prepared statements - Odyssey supports protocol-level prepared statements and YSQL Connection Manager inherits this feature.

## Start YSQL Connection Manager

To start a YugabyteDB cluster with YSQL Connection Manager, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `enable_ysql_conn_mgr` to true.

When `enable_ysql_conn_mgr` is set, each YB-TServer starts the YSQL Connection Manager process along with the PostgreSQL process. You should see one YSQL Connection Manager process per YB-TServer.

To create a single-node cluster with YSQL Connection Manager using [yugabyted](../../../reference/configuration/yugabyted/), use the following  command:

```sh
./bin/yugabyted start --tserver_flags "enable_ysql_conn_mgr=true,allowed_preview_flags_csv={enable_ysql_conn_mgr}" --ui false
```

Because `enable_ysql_conn_mgr` is a preview flag only, to use it, add the flag to the `allowed_preview_flags_csv` list (that is, `allowed_preview_flags_csv=enable_ysql_conn_mgr`).

{{< note >}}

To create a large number of client connections, ensure that "SHMMNI" (the maximum number of concurrent shared memory segments an OS allows) as well as [ulimit](../../../deploy/manual-deployment/system-config/#set-ulimits) is set correctly as follows:

1. Open the file `/etc/sysctl.conf`.
1. Add `kernel.shmmni = 32768` (support for 30000 clients) at the end of the file.
1. To refresh the settings, use `sudo sysctl -p`.
{{< /note >}}

### YugabyteDB Anywhere

{{<tags/feature/ea>}}To use built-in connection pooling with universes deployed using YugabyteDB Anywhere, turn on the **Connection pooling** option when [creating](../../../yugabyte-platform/create-deployments/create-universe-multi-zone/#advanced-configuration) or [modifying](../../../yugabyte-platform/manage-deployments/edit-universe/#edit-connection-pooling) a universe. When managing universes using YugabyteDB Anywhere, do not set connection pooling options using flags.

## Configuration

By default, when YSQL Connection Manager is enabled, it uses the port 5433, and the backend database is assigned a random free port.

To explicitly set a port for YSQL, you should specify ports for the flags `ysql_conn_mgr_port` and [ysql_port](../../../reference/configuration/yugabyted/#advanced-flags).

The following table describes YB-TServer flags related to YSQL Connection Manager:

| flag | Description | Default |
|:---- | :---------- | :------ |
| enable_ysql_conn_mgr | Enables YSQL Connection Manager for the cluster. YB-TServer starts a YSQL Connection Manager process as a child process. | false |
| enable_ysql_conn_mgr_stats | Enable statistics collection from YSQL Connection Manager. These statistics are displayed at the endpoint `<ip_address_of_cluster>:13000/connections`. | true |
| ysql_conn_mgr_idle_time | Specifies the maximum idle time (in seconds) allowed for database connections created by YSQL Connection Manager. If a database connection remains idle without serving a client connection for a duration equal to, or exceeding this value, it is automatically closed by YSQL Connection Manager. | 60 |
| ysql_conn_mgr_max_client_connections | Maximum number of concurrent client connections allowed. | 10000 |
| ysql_conn_mgr_max_conns_per_db | Maximum number of concurrent database connections YSQL Connection Manager can create per pool. If set to zero, get maximum connections from the `pgconf` file. 10% of connections are allocated for control connections, and 90% for global connections. | 0 |
| ysql_conn_mgr_min_conns_per_db | Minimum number of physical connections that is present in the pool. This limit is not considered while closing a broken physical connection. | 1 |
| ysql_conn_mgr_num_workers | Number of worker threads used by YSQL Connection Manager. If set to 0, the number of worker threads will be half of the number of CPU cores. | 0 |
| ysql_conn_mgr_stats_interval | Interval (in seconds) for updating the YSQL Connection Manager statistics. | 1 |
| ysql_conn_mgr_username | Username to be used by YSQL Connection Manager for creating database connections. | yugabyte |
| ysql_conn_mgr_password | Password to be used by YSQL Connection Manager for creating database connections. | yugabyte |
| ysql_conn_mgr_internal_conn_db | Database to which YSQL Connection Manager will make connections to execute internal queries. | yugabyte |
| ysql_conn_mgr_superuser_sticky | Make superuser connections sticky. | true |
| ysql_conn_mgr_port | YSQL Connection Manager port to which clients can connect. This must be different from the PostgreSQL port set via `pgsql_proxy_bind_address`. | 5433 |
| ysql_conn_mgr_server_lifetime | The maximum duration (in seconds) that a backend PostgreSQL connection managed by YSQL Connection Manager can remain open after creation. | 3600 |
| ysql_conn_mgr_log_settings | Comma-separated list of log settings for YSQL Connection Manger. Can include  'log_debug', 'log_config', 'log_session', 'log_query', and 'log_stats'. | "" |
| ysql_conn_mgr_use_auth_backend | Enable the use of the auth-backend for authentication of logical connections. When false, the older auth-passthrough implementation is used. | true |
