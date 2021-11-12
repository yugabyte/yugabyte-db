---
title: View slow YSQL queries
linkTitle: Slow YSQL queries
description: View slow running queries that have run on your Yugabyte Cloud cluster.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-queries-slow
    parent: cloud-monitor
    weight: 300
isTocNested: true
showAsideToc: true
---

Evaluate the performance of slow queries that have run on your cluster using the **Slow Queries** option on the cluster **Performance** tab. Use this data to:

- Visually identify slower running database operations.
- Evaluate query execution times over time.
- Discover potential queries for [tuning](../../../explore/query-1-performance/).

Slow queries are not available for YCQL.

![Cloud Cluster Slow Queries tab](/images/yb-cloud/cloud-clusters-slow.png)

To filter the query list, enter query text in the filter field. To sort the list by column, click the column heading. Click **Options** to select the columns to display.

To view query details, click the right-arrow button for the query in the list to display the **Query Details** sheet.
