---
title: View live queries in YugabyteDB Managed
headerTitle: View live queries
linkTitle: Live queries
description: View live queries running on your cluster.
headcontent: View live queries running on your cluster
menu:
  preview_yugabyte-cloud:
    identifier: cloud-queries-live
    parent: cloud-monitor
    weight: 200
type: docs
---

Evaluate the performance of running queries on your cluster using the **Live Queries** on the cluster **Performance** tab. You can use this data to:

- Visually identify relevant database operations.
- Evaluate query execution times.
- Discover potential queries for [tuning](../../../explore/query-1-performance/).

Live queries only shows queries "in-flight" (that is, currently in progress); queries that execute quickly might not show up by the time the display loads. There is no significant performance overhead on databases because the queries are fetched on-demand and are not tracked in the background.

![Cluster Live Queries](/images/yb-cloud/managed-monitor-live-queries.png)

You can choose between displaying YSQL and YCQL queries.

To filter the query list, enter query text in the filter field. To sort the list by column, click the column heading. Click **Edit Options** to select the columns to display.

To view query details, click the right-arrow button for the query in the list to display the **Query Details** sheet.

The following table describes the **Live Queries** column values.

| Column                         | Description                                                  |
| ------------------------------ | ------------------------------------------------------------ |
| Node Name                      | The name of the node where the query executed.               |
| Database /Keyspace             | The YCQL keyspace or YSQL database used by the query.        |
| Elapsed Time                   | The duration (in milliseconds) of the query handling.        |
| Status (YSQL)                  | The YSQL session status: idle, active, idle in transaction, fastpath function call, idle in transaction (aborted), or disabled. |
| Type (YCQL)                    | The YCQL query type: PREPARE, EXECUTE, QUERY, or BATCH       |
| Client Host                    | The address of the client that sent this query.              |
| Client Port                    | The port of the client that sent this query.                 |
