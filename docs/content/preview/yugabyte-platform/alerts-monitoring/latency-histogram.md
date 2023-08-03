---
title: View histogram and P99 latencies for slow queries
headerTitle: Latency histogram and P99 latencies
linkTitle: Latency histogram
description: View histogram and P99 latencies for slow queries
menu:
  preview_yugabyte-platform:
    parent: alerts-monitoring
    identifier: latency-histogram
    weight: 50
type: docs
---

Percentile metrics form the core set of metrics that enable users to measure query performance against SLOs (Service Level Objectives). Surfacing percentile metrics per normalized query and by Ops (Operations/second) type will enable users to measure query performance against SLOs. Additionally, these metrics can help identify performance issues efficiently and quickly.

YugabyteDB Anywhere addresses the above requirement and supports P99, P95, P90, P50 metrics for every query showing under the [Slow Queries](../../../yugabyte-platform/alerts-monitoring/slow-queries-dashboard/) UI.
You can view latency histograms for every YSQL query you run on one or multiple nodes of your universe and get an aggregated view of the metrics.

This feature is available for YBA version 2.18.2 or later with latency histogram support in YugabyteDB version 2.18.1 (or later), or 2.19.1 (or later).



