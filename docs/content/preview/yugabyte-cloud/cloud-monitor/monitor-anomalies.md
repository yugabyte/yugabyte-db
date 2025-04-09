---
title: YugabyteDB Aeon Performance Anomalies
headerTitle: Anomalies
linkTitle: Anomalies
description: Detect anomalies with your cluster performance.
headcontent: Detect anomalies in cluster performance
menu:
  preview_yugabyte-cloud:
    identifier: monitor-troubleshooting
    parent: cloud-monitor
    weight: 420
type: docs
---

Use Anomalies to monitor your cluster for anomalies in performance - whether with the database or applications.

For meaningful results, run your workload for at least an hour.

To monitor clusters in real time, use the performance metrics on the cluster [Overview and Performance](../overview/) tabs.

![Troubleshooting Platform](/images/yb-cloud/managed-monitor-advisor.png)

The dashboard is split into two parts - Cluster Load and Detected Anomalies.

## Cluster load

Use the cluster load chart to see at a glance the status of your cluster:

- Idle, active, or bottlenecked
- What type of load - CPU, I/O, or something else

The Y axis on the Cluster load chart is the number of active connections on the cluster. An active connection can be a YSQL process or a TServer thread, for example.

| Statistic | Description |
| :--- | :--- |
| Queries | Number of active queries |
| WaitOnCondition |
| Network |
| Lock |
| IO |
| CPU |
| Activity |

In a typical scenario, an application sends a query to a YSQL process, and that process contacts its local TServer. The TServer farms out the SQL to the appropriate nodes that have the data needed to satisfy the query. Therefore, a typical query requires at least two connections to the cluster: one for the YSQL process, and at least one TServer thread. (There can be multiple TServer threads active if the query has data on multiple nodes.)

The colors in the chart are typically CPU for the active TServer threads and TServerWait for those YSQL processes waiting for the TServer threads to complete their parts of the SQL query.

Queries (black line) shows the actual number of queries being run.

The bar chart shows how the connections are spending their time. Typically the TServer threads are running on CPU, and the YSQL process are waiting for those TServer threads on TServerWait.

If other waits show up as a significant portion of the bar chart that could indicate some kind of bottleneck.

## Detected anomalies

Detected anomalies shows potentially performance impacting anomalies.

| Category | Description |
| :------ | :---------- |
| App&nbsp;(application) | Anomalies that can only be addressed at the application level. For example, if the application is sending all its connections directly to one node in the cluster, this will lead to a load imbalance on that node. This can be addressed by using a load balancer or YSQL Connection Manager. |
| DB (database) | Issues internal to the database. This could include unused or redundant indexes, incorrect table partitioning (hash vs range), large tablets that need splitting, or hot tablets. |
| Node (cluster nodes) | Node-specific issues, such as one node with higher CPU or IO load (hot spot), or slow disk. |
| SQL (SQL queries) | Issues specific to particular SQL statements, such as when the latency of a statement gets significantly higher, high waits for locks, or excessive catalog reads. |

Each anomaly type shows the number of anomalies in each bucket.

Click the + to expand the category to show the individual anomalies. Click **Expand all** to expand all categories to show all the anomalies.

To see anomaly details, click the row. This displays a detailed chart for the specific anomaly.
