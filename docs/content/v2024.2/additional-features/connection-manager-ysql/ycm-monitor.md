---
title: YSQL Connection Manager metrics
headerTitle: Observability and metrics
linkTitle: Observability
description: YSQL Connection Manager observability and metrics
headcontent: Monitor YSQL Connection Manager
menu:
  v2024.2:
    identifier: ycm-monitor
    parent: connection-manager
    weight: 30
type: docs
---

## Metrics

Use the following metrics to monitor connections when using YSQL Connection Manager.

Access the per-pool metrics in JSON format at the `13000/connections` endpoint. Each pool is reported as a JSON object with the following fields.

| Metric Name | Description |
| :--- | :--- |
| database_name | Name of the database used by the pool. |
| DB OID | OID of the database used by the pool. |
| user_name | Name of the user used by the pool. |
| User OID | OID of the user used by the pool. |
| logical_rep | Whether the pool serves logical replication connections. |
| active_logical_connections | Specifies on a pool-by-pool basis the number of active logical (client) connections.<br>An "active" client connection corresponds to a session in an active transaction on a server connection. |
| queued_logical_connections | Specifies on a pool-by-pool basis the number of queued client connections.<br>A "queued" client connection corresponds to a session that is queued up to attach to a server connection. |
| waiting_logical_connections | Specifies on a pool-by-pool basis the number of waiting/idle client connections.<br>A session that is neither queued to attach to a server connection nor currently using a server connection is in a "waiting" state. |
| active_physical_connections | Specifies on a pool-by-pool basis the number of active physical (server) connections.<br>(At the start of a transaction) After a server connection is picked up from the connection pool (or freshly created) to serve a client connection, it is marked as "active". |
| idle_physical_connections | Specifies on a pool-by-pool basis the number of idle server connections.<br>(At the end of a transaction) Once a server connection detaches from its client connection and returns to the server connection pool, it is marked as "idle". |
| sticky_connections | Specifies on a pool-by-pool basis the number of [sticky connections](../ycm-setup/#sticky-connections).<br>server connections that do not return to the connection pool at the end of a transaction remain stuck to the client connection for the lifetime of the session. |
| avg_wait_time_ns | Specifies on a pool-by-pool basis the average time (in ns) for a client connection to attach to a server connection — time spent queued plus the time taken to find and attach a server connection. Averaged over the last stats interval. |
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

## Prometheus metrics

YSQL Connection Manager also exposes metrics in Prometheus format at the YB-TServer Prometheus metrics endpoint, for scraping by monitoring systems such as Prometheus and Grafana.

The following server-level metrics describe the Connection Manager as a whole.

| Metric | Type | Description |
| :----- | :--- | :---------- |
| `ysql_conn_mgr_num_pools` | gauge | Number of YSQL Connection Manager pools. |
| `ysql_conn_mgr_last_updated_timestamp` | gauge | Timestamp of the last update to YSQL Connection Manager metrics. |
| `ysql_conn_mgr_max_client_connections` | gauge | Maximum number of clients that can connect to YSQL Connection Manager. |

The following metrics are reported per pool, labelled with the `database` and `user` of the pool.

| Metric | Type | Description |
| :----- | :--- | :---------- |
| `ysql_conn_mgr_active_clients` | gauge | Number of active logical connections. |
| `ysql_conn_mgr_queued_clients` | gauge | Number of logical connections waiting in the queue for a physical connection. |
| `ysql_conn_mgr_waiting_clients` | gauge | Number of logical connections that are either idle (no ongoing transaction) or waiting for the worker thread to be processed. |
| `ysql_conn_mgr_active_servers` | gauge | Number of physical connections currently attached to a logical connection. |
| `ysql_conn_mgr_idle_servers` | gauge | Number of physical connections not attached to any logical connection. |
| `ysql_conn_mgr_query_rate` | gauge | Query rate over the last stats interval (set in the Odyssey config). |
| `ysql_conn_mgr_transaction_rate` | gauge | Transaction rate over the last stats interval (set in the Odyssey config). |
| `ysql_conn_mgr_avg_wait_time_ns` | gauge | Average wait time (in nanoseconds) for a logical connection to be attached to a physical connection. |
| `ysql_conn_mgr_sticky_connections` | gauge | Number of logical connections attached to a physical connection for the lifetime of the logical connection. |

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
