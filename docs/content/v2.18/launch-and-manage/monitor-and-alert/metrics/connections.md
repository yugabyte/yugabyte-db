---
title: Connection metrics
headerTitle: Connections
linkTitle: Connection metrics
headcontent: Monitor YSQL connections
description: Learn about YugabyteDB's connection metrics, and how to select and use the metrics.
menu:
  stable:
    identifier: connections
    parent: metrics-overview
    weight: 110
type: docs
---

Connection metrics represent the cumulative number of connections to the YSQL backend per node. This includes various background connections, such as checkpointer, active connections count that only includes the client backend connections, newly established connections, and connections rejected over the maximum connection limit.

Connection metrics are only available in Prometheus format.

The following table describes key connection metrics. All units are in number of connections.

| Metric | Type | Description |
| :----- | :--- | :---------- |
| `yb_ysqlserver_active_connection_total` | gauge | The number of active client backend connections to YSQL server. If a client connection is executing a statement, it is considered an active connection. Any client connection not executing a statement is considered an idle connection.|
| `yb_ysqlserver_connection_total` | gauge | The total number of all connections to YSQL, which includes active connections, idle connections, and background connections. |
| `yb_ysqlserver_max_connection_total` | gauge | The maximum number of concurrent connections that a YSQL server can support at any given time. The default is 300. This value can be changed using the `--ysql_max_connections` YB-TServer flag. |
| `yb_ysqlserver_connection_over_limit_total` | counter | The number of connection requests rejected by the YSQL server over the maximum connection limit, based on `yb_ysqlserver_max_connection_total`.  |
| `yb_ysqlserver_new_connection_total` | counter | The total number of connections established with the YSQL server since the start of the process.  |

These metrics can be aggregated across the entire cluster using appropriate aggregations.
