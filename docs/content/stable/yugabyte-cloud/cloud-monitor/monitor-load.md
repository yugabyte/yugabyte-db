---
title: Monitor cluster load
headerTitle: Cluster Load
linkTitle: Cluster Load
description: Detect anomalies in cluster performance.
headcontent: Discover whether the system was overloaded, and why
tags:
  feature: tech-preview
menu:
  stable_yugabyte-cloud:
    identifier: monitor-load
    parent: monitor-performance
    weight: 350
type: docs
---

Monitor your cluster load using the **Cluster Load** dashboard. The dashboard displays data from the [Active Session History](../../../launch-and-manage/monitor-and-alert/active-session-history-monitor/) in the **Cluster Load Overview** chart. (This is also the primary data source used for [anomaly detection](../aeon-anomalies/).) Use this view to answer the question _Was the system overloaded, and why_.

The dashboard shows the status of your cluster at a glance:

- When it was idle, active, or bottlenecked.
- What type of load - CPU, I/O, or something else.

Load monitoring is only available for clusters running YugabyteDB v2024.2.7 and later.

{{< note title="Tech Preview" >}}

Load monitoring is currently in Tech Preview. To try the feature, contact {{% support-cloud %}}.

{{< /note >}}

To view cluster load, navigate to the cluster **Performance** tab and choose **Cluster Load**. You can choose between **Overall Load** and **Load by Database**.

![Cluster Load](/images/yb-cloud/monitor-load.png)

## Overall Load

### Cluster Load Overview

The top section shows the cluster load chart. The bars show the number of active connections to the cluster over time. These connections can be YSQL (PostgreSQL) processes or TServer threads. The black line represents the number of running queries over time.

Each bar shows the state of connections, as follows.

| Connection State | Description |
| :--- | :--- |
| CPU | Query is running normally. |
| Client | Waiting for the client to either read results or send more data. |
| DiskIO | Reading or writing from storage, such as writing to WAL or reading tablets from storage. |
| IO | [Waiting for description] |
| Lock | Waiting on a lock. |
| RPCWait | RPC waits. |
| Timeout | Waiting for `pgsleep()` function. |
| TServerWait | Waiting for TServer threads to complete. |
| WaitOnCondition | [Waiting for description] |

The colors in the load chart indicate where time is being spent, such as CPU, I/O, locks, or RPC wait. This provides a breakdown of what is contributing to the overall load.

A key signal in this chart is CPU usage. The green portion of the bars represents CPU demand, while the dashed gray line represents total CPU capacity (that is, the number of vCPUs in the cluster). When CPU demand exceeds this line, the cluster is CPU-bound and queries are competing for CPU resources. In that case, performance can be improved either by adding more CPU capacity or by optimizing the queries that are consuming the most CPU.

The colors in the chart are typically CPU for the active TServer threads and TServerWait for those YSQL processes waiting for the TServer threads to complete their parts of the SQL query.

If other waits show up as a significant portion of the bar chart that could indicate some kind of bottleneck.

### Queries

The bottom section of the dashboard shows the top SQL statements contributing to the load seen in the Load chart, helping identify which queries are driving resource usage.

Select a query in the list to view details. This includes cluster load for the selected query, performance metrics, and tables and tablets accessed.

A typical query involves multiple components. An application sends a query to a YSQL process, which then communicates with its local TServer. The TServer may fan the query out to other nodes that hold the required data. As a result, even a single query often results in multiple active connections, at least one YSQL process, and one or more TServer threads.

## Load by Database

### Activity by database

The top section of the dashboard shows the Activity by Database chart, showing the number of active sessions for each cluster database through time.

### Events Load by Database

The bottom section of the dashboard shows the event load for each database.

Click the + to view a breakdown of database load by query. Select a query in the list to view details. This includes cluster load for the query, performance metrics, and tables and tablets accessed.
