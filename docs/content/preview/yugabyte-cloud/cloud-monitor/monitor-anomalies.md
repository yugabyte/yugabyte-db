---
title: YugabyteDB Aeon Performance Anomalies
headerTitle: Anomalies
linkTitle: Anomalies
description: Detect anomalies with your cluster performance.
headcontent: Detect anomalies in cluster performance
tags:
  feature: tech-preview
menu:
  preview_yugabyte-cloud:
    identifier: monitor-troubleshooting
    parent: cloud-monitor
    weight: 420
type: docs
---

Use Anomalies to monitor your cluster for anomalies in performance - whether with the database or applications.

{{< note title="Tech Preview" >}}

Anomalies is currently in Tech Preview. To try the feature, contact {{% support-cloud %}}.

{{< /note >}}

To view anomalies, click the cluster **Perf Advisor** tab and choose **Anomalies**.

![Anomalies](/images/yb-cloud/managed-monitor-anomalies.png)

The dashboard is split into two parts - the top section shows the **Cluster Load**. The bottom section shows the section details - currently only **Detected Anomalies**.

## Cluster load

The Cluster load chart shows the number of active connections to the cluster (bars) and the number of running queries (black line) over time. Use this view to answer the question _Was the system overloaded, and why_.

The view shows the status of your cluster at a glance:

- When it was idle, active, or bottlenecked
- What type of load - CPU, I/O, or something else

Each bar shows the connections broken down by state.

| Connection State | Description |
| :-------- | :---------- |
| WaitOnCondition |  |
| Timeout | Waiting for `pgsleep()` function |
| TServerWait | Waiting for TServer threads to complete |
| Network |  |
| Lock    | Waiting on a lock |
| IO      | Reading or writing from storage, such as writing to WAL or reading tablets from storage |
| CPU     | Query is running normally |
| Client  | Waiting for client to either read results or send more data |

In a typical scenario, an application sends a query to a YSQL process, and that process contacts its local TServer. The TServer farms out the SQL to the appropriate nodes that have the data needed to satisfy the query. Therefore, a typical query requires at least two connections to the cluster: one for the YSQL process, and at least one TServer thread. (There can be multiple TServer threads active if the query has data on multiple nodes.)

The colors in the chart are typically CPU for the active TServer threads and TServerWait for those YSQL processes waiting for the TServer threads to complete their parts of the SQL query.

Queries (black line) shows the actual number of queries being run.

The bar chart shows how the connections are spending their time. Typically the TServer threads are running on CPU, and the YSQL process are waiting for those TServer threads on TServerWait.

If other waits show up as a significant portion of the bar chart that could indicate some kind of bottleneck.

## Detected anomalies

Detected anomalies shows potential performance impacting anomalies through time, by type. Use this view to answer the question _When did problems occur, and what changed at that time_.

Each anomaly type shows the number of anomalies in each bucket.

Click the + to expand the category to show the individual anomalies. Click **Expand all** to expand all categories to show all the anomalies under each type.

To see anomaly details, click the row. This displays a detailed chart for the specific anomaly.

| Type | Description |
| :------ | :---------- |
| [App&nbsp;(application)](#application-anomalies) | Anomalies that can only be addressed at the application level. For example, if the application is sending all its connections directly to one node in the cluster, this will lead to a load imbalance on that node. This can be addressed by using a load balancer or YSQL Connection Manager. |
| [DB (database)](#database-anomalies) | Issues internal to the database. This could include unused or redundant indexes, incorrect table partitioning (hash vs range), large tablets that need splitting, or hot tablets. |
| [Node (cluster nodes)](#node-anomalies) | Node-specific issues, such as one node with higher CPU or IO load (hot spot), or slow disk. |
| [SQL (SQL queries)](#sql-anomalies) | Issues specific to particular SQL statements, such as when the latency of a statement gets significantly higher, high waits for locks, or excessive catalog reads. |

### Application anomalies

Connections Uneven
: SQL connections are spread unevenly across the nodes. If connections are not balanced across nodes, a load balancer may be required to prevent node hotspots.

### Database anomalies

Use Range Index
: A HASH index was found where RANGE index would be more suitable. Schema mismatch can cause poor performance for range queries.

Large Tablet
: One or more tablets in a table are significantly larger than the average of other tablets, possibly causing uneven compactions or query times. In this case the tablet should probably be split.

Redundant/Unused Index
: A redundant or unused index was found. These can add write overhead and bloat memory use.

Uneven IO
: Significantly more read/write requests to the table are being sent to only a few tablets, compared to the average. May indicate shard-level skew or a hot shard.

### Node anomalies

#### Slow IO

Triggered when IO wait time is greater than 90%, or IO queue depth is more than 10.

Determine if IO latency increased (IO bottleneck) or if demand increased (runaway queries).

Next steps:

- Investigate top queries by IO time
- Check storage layer latency

#### Uneven Data

Table data is spread unevenly across the nodes.

#### Uneven CPU

CPU use is unbalanced across the nodes. Triggered when a node's CPU is >80% and >50% above the cluster average.

Solutions:

- Add CPU cores
- Optimize or redistribute heavy SQL workloads

#### Uneven IO

The read and write distribution is is unbalanced across the nodes. Triggered when a node has >10% skew in read/write ops or query activity.

Often caused by hash distribution issues or application connection imbalance.

#### Uneven SQL

SQL queries are spread unevenly across the nodes.

### SQL anomalies

#### SQL Latency

This is triggered when latency doubles for a query that previously ran >20ms and >0.2 execs/sec.

Possible causes include:

- CPU/IO/Memory resource pressure
- Lock contention
- Plan regression
- Retry loops from read restarts (only document when we can expose metrics)

Investigation steps:

- Check if overall cluster load changed
- Drill into the anomaly and compare SQL and storage events
- Run EXPLAIN ANALYZE to check execution plan

#### Catalog Reads

Triggered when >50% of wait time is due to Catalog Read waits.

Causes:

- High new connection churn (each new connection triggers CatalogReads)
- Cache misses on table/index metadata

Solutions:

- Use a connection pool or manager
- Pre-cache target tables using ysql_catalog_preload_additional_table_list
- Enable prepared statements for repeated queries

#### Locks

Triggered when significant time is spent waiting for locks.

Solutions:

- Identify blocking sessions and terminate if needed
- Investigate application logic for unnecessary locking
