---
title: View live queries
linkTitle: View live queries
description: View live queries running on your cluster.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: queries-live
    parent: cloud-monitor
    weight: 200
isTocNested: true
showAsideToc: true
---

You can monitor and display current running queries on your cluster to evaluate their performance. You can use this data to:

- Visually identify relevant database operations
- Evaluate query execution times
- Discover potential queries for performance optimization

There is no significant performance overhead on databases because the queries are fetched on-demand and are not tracked in the background.

![Cloud Cluster Live Queries tab](/images/yb-cloud/cloud-clusters-live.png)

To filter the query list, enter query text in the filter filed. To sort the list by column, click the column heading. Click **Options** to select the columns to display.

To view query details, click the right-arrow button for the query in the list to display the **Query Details** sheet.
