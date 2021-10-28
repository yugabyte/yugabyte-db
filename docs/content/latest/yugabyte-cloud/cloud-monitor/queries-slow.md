---
title: View slow YSQL queries
linkTitle: View slow queries
description: View slow running queries that have run on your Yugabyte Cloud cluster.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: queries-slow
    parent: cloud-monitor
    weight: 300
isTocNested: true
showAsideToc: true
---

You can monitor and display slow queries that have run on your cluster to evaluate their performance. Use this data to:

- Visually identify slower running database operations.
- Evaluate query execution times over time.
- Discover potential queries for [tuning](../../../explore/query-1-performance/).

Slow queries are not available for YCQL.

![Cloud Cluster Live Queries tab](/images/yb-cloud/cloud-clusters-slow.png)

To filter the query list, enter query text in the filter filed. To sort the list by column, click the column heading. Click **Options** to select the columns to display.

To view query details, click the right-arrow button for the query in the list to display the **Query Details** sheet.
