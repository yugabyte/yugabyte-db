---
title: Connection metrics
headerTitle: Connections
linkTitle: Connections
headcontent: Monitor and manage clusters with YugabyteDB's connection metrics.
description: Learn about YugabyteDB's connection metrics, and how to select and use the metrics.
menu:
  preview:
    identifier: connections
    parent: metrics-overview
    weight: 110
type: docs
---

Connection metrics represent the cumulative number of connections to YSQL backend per node. This includes various background connections, such as checkpointer, active connections count that only includes the client backend connections, newly established connections, and connections rejected over the maximum connection limit. By default, YugabyteDB can have up to 10 simultaneous connections per vCPU. Connection metrics are only available in Prometheus format.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `yb_ysqlserver_active_connection_total` | connections | counter | The number of active client backend connections to YSQL. |
| `yb_ysqlserver_connection_total` | connections | counter | The number of all connections to YSQL. |
| `yb_ysqlserver_max_connection_total` | connections | counter | The number of maximum connections that can be supported by a node at a given time. |
| `yb_ysqlserver_connection_over_limit_total` | connections | counter | The number of connections rejected over the maximum connection limit has been reached. |

These metrics can be aggregated across the entire cluster using appropriate aggregations.
