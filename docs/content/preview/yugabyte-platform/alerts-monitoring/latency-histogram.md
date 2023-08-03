---
title: View latency histogram and P99 latency metrics for slow queries
headerTitle: Latency histogram and P99 latencies
linkTitle: Latency histogram
description: View histogram and P99 latency metrics for slow queries
menu:
  preview_yugabyte-platform:
    parent: alerts-monitoring
    identifier: latency-histogram
    weight: 50
type: docs
---

Percentile metrics form the core set of metrics that enable users to measure query performance against SLOs (Service Level Objectives). Surfacing percentile metrics per normalized query and by Ops (Operations/second) type enables you to measure query performance against SLOs. Additionally, these metrics can help identify performance issues efficiently and quickly.

You can view P99, P95, P90, and P50 metrics for every query displayed on the [Slow Queries](../../../yugabyte-platform/alerts-monitoring/slow-queries-dashboard/) dashboard.
You can view latency histograms for every YSQL query you run on one or multiple nodes of your universe and get an aggregated view of the metrics.

As slow queries are only available for YSQL, this feature is available for YSQL only from YBA version 2.18.2 or later with latency histogram support in YugabyteDB version 2.18.1 (or later), or 2.19.1 (or later).

To view the latency histogram and P99 metrics, access the [Slow Queries](../../../yugabyte-platform/alerts-monitoring/slow-queries-dashboard/) dashboard and run YSQL queries using the following steps:

1. Navigate to the **Universes**, select your universe, then select **Queries > Slow Queries**.
1. Enable the **Query monitoring** toggle button (disabled by default).
1. Run some queries on your YBA universe by clicking on or more query options from the **Slow Queries** tab in the left navigation panel.
    \
    You can see the query details listing the P25, P50, P90, P95, and P99 latency metrics as per the following illustration.

    ![latency-histogram1](/images/yp/alerts-monitoring/slow-queries/latency-histogram1.png)

{{< note title="Note" >}}

- If latency histogram is not reported from YBA, then the histogram graph is omitted from the Slow Query dashboard.
- If the P99 latency statistics are not reported from YBA, then `-` placeholder gets displayed for the same in the **Query Details** tab.

{{< /note >}}

## Learn more

- [Slow Query dashboard](../../../yugabyte-platform/alerts-monitoring/slow-queries-dashboard/) for details on how to run the queries and view the results based on the available options.
