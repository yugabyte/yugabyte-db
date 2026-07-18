---
title: YSQL Connection Manager metrics
headerTitle: Observability and metrics
linkTitle: Observability
description: YSQL Connection Manager observability and metrics
headcontent: Monitor YSQL Connection Manager
menu:
  v2025.1:
    identifier: ycm-monitor
    parent: connection-manager
    weight: 30
type: docs
---

## Metrics

Use the following metrics to monitor connections when using YSQL Connection Manager.

Access metrics at the `13000/connections` endpoint.

| Metric Name | Description |
| :--- | :--- |
| database_name (DB OID) | Specifies information regarding the database being used in each pool. |
| user_name (User OID) | Specifies information regarding the user being used in each pool. |
| active_logical_connections | Specifies on a pool-by-pool basis the number of active logical (client) connections.<br>An "active" client connection corresponds to a session in an active transaction on a server connection. |
| queued_logical_connections | Specifies on a pool-by-pool basis the number of queued client connections.<br>A "queued" client connection corresponds to a session that is queued up to attach to a server connection. |
| waiting_logical_connections | Specifies on a pool-by-pool basis the number of waiting/idle client connections.<br>A session that is neither queued to attach to a server connection nor currently using a server connection is in a "waiting" state. |
| active_physical_connections | Specifies on a pool-by-pool basis the number of active physical (server) connections.<br>(At the start of a transaction) After a server connection is picked up from the connection pool (or freshly created) to serve a client connection, it is marked as "active". |
| idle_physical_connections | Specifies on a pool-by-pool basis the number of idle server connections.<br>(At the end of a transaction) Once a server connection detaches from its client connection and returns to the server connection pool, it is marked as "idle". |
| sticky_connections | Specifies on a pool-by-pool basis the number of [sticky connections](../ycm-setup/#sticky-connections).<br>server connections that do not return to the connection pool at the end of a transaction remain stuck to the client connection for the lifetime of the session. |
| avg_wait_time_ns | Specifies on a pool-by-pool basis the time (in ns) on average clients have to be queued before attaching to a server connection. |
| qps / tps | Specifies on a pool-by-pool basis some basic performance metrics.<br>qps = queries per second<br>tps = transactions per second |

### Logical and server connections

The sum of waiting, queued, and active client connections provides the number of client connections that are currently open.

The sum of idle and active server connections provides the number of server-side backend processes that have been spawned.

The number of active client connections will always be equal to the number of active server connections.

### Pool use (idle server connections/waiting client connections)

In general, you can have idle server connections, as they can be used for connection burst scenarios. Configure the [timeout for idle connections](../ycm-setup/#configure) using the `ysql_conn_mgr_idle_time` flag, depending on your use case.

You can reduce `ysql_max_connections` such that the active to idle ratio is higher, provided that idle connections are not completely extinguished in the long run.

### Queued clients

You can have some queued state clients. However, if clients start timing out or query latency is too high, increase `ysql_max_connections`.

### Sticky connections

[Sticky connections](../ycm-setup/#sticky-connections) can be the cause of higher connection acquisition latency in some cases (sticky connections are destroyed once used).

They may also be the cause for connection exhaustion or client wait timeouts.

## Logging

Connection Manager provides the following log levels that you can set using the `ysql_conn_mgr_log_settings` flag:

- log_debug
- log_query
- log_config
- log_session
- log_stats

The structure of a log line is as follows:

```prolog
PID YYYY-MM-DD HH:MM:SS UTC log_level [clientID serverID] (context) This is a sample log!
```

For example:

```prolog
2986790 2025-04-22 20:55:08.236 UTC debug [c960b6b7a6030 scb5ee95439f2] (reset) ReadyForQuery
```

Connection Manager logs are stored in the same directory as [TServer logs](../../../explore/observability/logging/), depending on your cluster setup. They are rotated daily and have the following file naming convention:

```sh
ysql-conn-mgr-YYYY-MM-DD_HHMMSS.log.PID
```

For example:

```sh
ysql-conn-mgr-2025-04-22_205456.log.2986790
```

The log file was created for a Connection Manager process with a PID of 2986790, which started logging at 20:54:56 UTC on 22nd April 2025.

<!--
Monitoring in YBA & YBM (screenshots)
YBA :
YBM :
Metrics displayed on cluster UI (Available Per node, Per DB):
Client Connections
Client connection Average wait time (ns)
Server connections
Database transactions / sec

The following metrics are exported when Metrics Export is enabled in YugabyteDB Aeon:

- ybdb_ysql_conn_mgr_active_clients
- ybdb_ysql_conn_mgr_active_servers
- ybdb_ysql_conn_mgr_avg_wait_time_ns
- ybdb_ysql_conn_mgr_idle_servers
- ybdb_ysql_conn_mgr_last_updated_timestamp
- ybdb_ysql_conn_mgr_num_pools
- ybdb_ysql_conn_mgr_query_rate
- ybdb_ysql_conn_mgr_queued_clients
- ybdb_ysql_conn_mgr_sticky_connections
- ybdb_ysql_conn_mgr_transaction_rate
- ybdb_ysql_conn_mgr_waiting_clients
-->
