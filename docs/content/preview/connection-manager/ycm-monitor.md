---
title: YSQL Connection Manager metrics
headerTitle: Observability and metrics
linkTitle: Observability
description: YSQL Connection Manager observability and metrics
headcontent: Monitor YSQL Connection Manager
menu:
  preview:
    identifier: ycm-monitor
    parent: connection-manager
    weight: 30
type: docs
---

| Metric Name | Description |
| :--- | :--- |
| database_name (DB OID) | Specifies information regarding the database being used in each pool. |
| user_name (User OID) | Specifies information regarding the user being used in each pool. |
| active_logical_connections | Specifies on a pool-by-pool basis the number of active logical (client) connections.<br>An "active" logical connection corresponds to a session in an active transaction on a physical connection. |
| queued_logical_connections | Specifies on a pool-by-pool basis the number of queued logical connections.<br>A "queued" logical connection corresponds to a session that is queued up to attach to a physical connection. |
| waiting_logical_connections | Specifies on a pool-by-pool basis the number of waiting/idle logical connections.<br>A session that is neither queued to attach to a physical connection nor currently using a physical connection is in a "waiting" state. |
| active_physical_connections | Specifies on a pool-by-pool basis the number of active physical (server) connections.<br>(At the start of a transaction) Once a server connection is picked up from the connection pool (or freshly created) to serve a logical connection, it is marked as "active". |
| idle_physical_connections | Specifies on a pool-by-pool basis the number of idle physical connections.<br>(At the end of a transaction) Once a server connection detaches from its logical connection and returns to the physical connection pool, it is marked as "idle". |
| sticky_connections | Specifies on a pool-by-pool basis the number of sticky connections.<br>Physical connections that do not return to the connection pool at the end of a transaction remain "stuck" to the logical connection for the lifetime of the session. |
| avg_wait_time_ns | Specifies on a pool-by-pool basis the time (in ns) on average clients have to be "queued" before attaching to a physical connection. |
| qps / tps | Specifies on a pool-by-pool basis some basic performance metrics.<br>qps = queries per second<br>tps = transactions per second |

Some further implications from these metrics:

- Interaction between logical and physical connection metrics
  - The sum of waiting, queued and active logical connections signify the number of client connections that are currently open
  - The sum of idle and active physical connections signify the number of server-side backend processes that have been spawned
  - The number of active logical connections will always be equal to the number of active physical connections
- Pool utilization (idle physical conns/waiting logical conns)
  - Usually okay to have idle physical conns, good for connection burst scenarios - timeout for these can be configured depending on use case.
  - It makes sense to reduce `ysql_max_connections` such that active:idle ratio is higher, provided that idle connections are not completely extinguished in the long run.
- Queued clients
  - Usually okay to have some "queued" state clients. However, if clients start timing out/query latency is too high for customer - `ysql_max_connections` needs to be increased
- Sticky connections
  - Can be the cause for higher connection acquisition latency in some cases (sticky connections are destroyed once used)
  - Can be the cause for connection exhaustion/client wait timeout scenarios

## Logging

Connection Manager offers various log levels that can be set using the flag `ysql_conn_mgr_log_settings`:

- log_debug
- log_query
- log_config
- log_session
- log_stats

The structure of a log line is as follows:

```sh
PID YYYY-MM-DD HH:MM:SS UTC log_level [clientID serverID] (context) This is a sample log!
```

For example:

```sh
2986790 2025-04-22 20:55:08.236 UTC debug [c960b6b7a6030 scb5ee95439f2] (reset) ReadyForQuery
```

Connection Manager logs are found in the same directory as TServer logs, depending on your cluster setup. They are rotated daily and can be found as per the following file naming convention:

```sh
ysql-conn-mgr-YYYY-MM-DD_HHMMSS.log.PID
```

For example:

```sh
ysql-conn-mgr-2025-04-22_205456.log.2986790
```

is a log file created for a Connection Manager process with a PID of 2986790, which started logging at 20:54:56 UTC on 22nd April 2025.

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
