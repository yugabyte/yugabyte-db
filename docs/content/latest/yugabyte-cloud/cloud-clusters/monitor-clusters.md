<!---
title: Monitor performance
linkTitle: Monitor performance
description: Monitor Yugabyte Cloud clusters.
headcontent:
image: /images/section_icons/deploy/enterprise.png
aliases:
  - /latest/deploy/yugabyte-cloud/monitor-clusters/
menu:
  latest:
    identifier: monitor-clusters
    parent: cloud-clusters
    weight: 400
isTocNested: true
showAsideToc: true
--->

The **Performance** tab provides charts of a variety of performance metrics, and allows you to view live and slow running queries.

![Cloud Cluster Performance tab](/images/yb-cloud/cloud-clusters-metrics.png)

## Metrics

The **Metrics** sub-tab provides charts of the following performance metrics:

- Operations
- Latency
- CPU
- Memory
- System Load
- Disk Bytes/Sec
- Network Errors
- Overall RPCs
- Average SS Tables

Click **Options** to choose the metrics charts to display.

## Live Queries

The **Live Queries** sub-tab lists currently running queries on the cluster. Use the Live Queries list to monitor and display current running queries on the cluster. You can use this data to:

- Visually identify relevant database operations.
- Evaluate query execution times.
- Discover potential queries for performance optimization.

You can display YSQL or YCQL queries, and filter the list by text or column.

Select a query in the list to view the query details.

To change the columns that are displayed, click **Options**.

## Slow Running Queries

The **Slow Running Queries** sub-tab lists currently running queries on the cluster. Use the Slow Queries list to analyze statistics of past queries on the cluster. You can use this data to:

- Visually identify slower running database operations.
- Evaluate query execution times over time.
- Discover potential queries for memory optimization.

You can display YSQL or YCQL queries, and filter the list by text or column.

Select a query in the list to view the query details.

To change the columns that are displayed, click **Options**.
