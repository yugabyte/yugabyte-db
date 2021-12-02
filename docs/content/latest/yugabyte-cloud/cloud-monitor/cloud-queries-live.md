---
title: View live queries
linkTitle: Live queries
description: View live queries running on your cluster.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-queries-live
    parent: cloud-monitor
    weight: 200
isTocNested: true
showAsideToc: true
---

Evaluate the performance of running queries on your cluster using the **Live Queries** on the cluster **Performance** tab. You can use this data to:

- Visually identify relevant database operations.
- Evaluate query execution times.
- Discover potential queries for [tuning](../../../explore/query-1-performance/).

Live queries only shows queries "in-flight" (that is, currently in progress); queries that execute quickly might not show up by the time the display loads. There is no significant performance overhead on databases because the queries are fetched on-demand and are not tracked in the background.

![Cloud Cluster Performance Live Queries](/images/yb-cloud/cloud-clusters-live.png)

You can choose between displaying YSQL and YCQL queries.

To filter the query list, enter query text in the filter field. To sort the list by column, click the column heading. Click **Options** to select the columns to display.

To view query details, click the right-arrow button for the query in the list to display the **Query Details** sheet.
