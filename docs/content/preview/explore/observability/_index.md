---
title: Observability in YugabyteDB
headerTitle: Observability
linkTitle: Observability
description: Observability in YugabyteDB.
headcontent: Monitoring, Alerting and Analyzing in YugabyteDB.
image: /images/section_icons/secure/authorization.png
aliases:
  - /preview/explore/observability-docker/macos
  - /preview/explore/observability-docker/linux
  - /preview/explore/observability-docker/docker
menu:
  preview:
    identifier: explore-observability
    parent: explore
    weight: 310
type: indexpage
---

Observability refers to the extent to which the internal state and behavior of a system can be understood, monitored, and analyzed from the outside, typically by developers and DevOps. It focuses on providing insight into how a system is performing, what is happening inside it, and how it is interacting with its environment.

The goal of observability is to make it easier to diagnose and resolve issues, optimize performance, and gain insights into the system's behavior. It is especially crucial in modern, complex, and distributed systems, where understanding the interactions between different services and components can be challenging. [DevOps Research and Assessment (DORA)](https://dora.dev/) research shows that a comprehensive monitoring and observability solution, along with several other technical practices, positively contributes to the management of software infrastructure.

YugabytedDB provides several components and features that you can use to actively monitor your system and diagnose issues quickly.

## Metrics

YugabyteDB exports various [metrics](../../launch-and-manage/monitor-and-alert/metrics/#frequently-used-metrics) which are effectively quantitative measurements of the cluster's performance and behavior. These metrics include details on latency, connections, cache, consensus, replication, response times, resource usage, and more. We have organized them into different categories for easy exploration.

- [Throughput and latency metrics](../../launch-and-manage/monitor-and-alert/metrics/throughput)
- [Connection metrics](../../launch-and-manage/monitor-and-alert/metrics/connections)
- [Cache and storage metrics](../../launch-and-manage/monitor-and-alert/metrics/cache-storage)
- [Raft and distributed system metrics](../../launch-and-manage/monitor-and-alert/metrics/raft-dst)
- [Replication metrics](../../launch-and-manage/monitor-and-alert/metrics/replication)
- [YB-Master metrics](../../launch-and-manage/monitor-and-alert/metrics/ybmaster)

These provide a way to track trends and identify performance issues and effectively manage the system's performance and reliability.

## Alerting and Monitoring

Monitoring involves continuously checking the system's health and performance and notifying stakeholders if any issues arise. For this, you can set up automated alerts based on predefined thresholds or conditions. All metrics exposed by YugabytedDB are exportable to third-party monitoring tools like [Prometheus](./prometheus-integration/) and [Grafana](./grafana-dashboard/) which provide industry-standard alerting functionalities.

{{<note>}}
Both [YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/) and [Yugabytedb Managed](../../yugabyte-cloud/cloud-monitor/cloud-alerts/) provide a full suite of alerting capabilities for monitoring.
{{</note>}}

## Visualization and Analysis

YugabytedB provides dashboards that include charts, graphs, and other visual representations of the system's state and performance. [yugabyted](../..//reference/configuration/yugabyted/) starts a web-UI on port [15433](http://127.0.0.1:15433/performance/metrics?interval=lasthour&nodeName=all&showGraph=operations&showGraph=latency&showGraph=cpuUsage&showGraph=diskUsage&showGraph=totalLiveNodes) that displays different charts for various metrics.

You can also export the metrics provided by YugabyteDB onto third-party visualization tools like [Prometheus](./prometheus-integration/) and [Grafana](./grafana-dashboard/) as per the needs of your organization.

{{<note>}}
Both [YugabyteDB Anywhere](../../yugabyte-platform/troubleshoot/universe-issues/#use-metrics) and [Yugabytedb Managed](../../yugabyte-cloud/cloud-monitor/overview/) come with a full suite of visualizations to help you monitor your cluster and troubleshoot issues.
{{</note>}}

## Logging

Logs from different services i.e. [TServer](../../troubleshoot/nodes/check-logs/#yb-tserver-logs) and [Master](../../troubleshoot/nodes/check-logs/#yb-master-logs) provide a historical record of what has happened and can be very useful in debugging and troubleshooting. These logs are regularly rotated based on size which can be configured as described in [Logs Management](../../troubleshoot/nodes/check-logs#logs-management)

## Query Level Metrics

You can view the live queries via [pg_stat_activity](../query-1-performance/pg-stat-activity) and get the query statistics (eg. _time spent by a query_) using [pg_stat_statements](../query-1-performance/pg-stat-statements). These can be very useful in improving the performance of a specific query.

You can also get more details about the various steps of a query execution using the [Explain Analyze](../query-1-performance/explain-analyze) command.

## Learn more

{{<index/block>}}
  {{<index/item
      title="Prometheus integration"
      body="Export YugabyteDB metrics into Prometheus to inspect various metrics."
      href="./prometheus-integration/macos/"
      icon="fa-solid fa-chart-line">}}

  {{<index/item
      title="Grafana dashboard"
      body="Create dashboards using Prometheus metrics to understand the health and performance of YugabyteDB clusters."
      href="./grafana-dashboard/grafana/"
      icon="fa-solid fa-chart-bar">}}

  {{<index/item
      title="Query statistics using pg_stat_statements"
      body="Track planning and execution metrics for SQL statements"
      href="../query-1-performance/pg-stat-statements"
      icon="fa-solid fa-signal">}}

  {{<index/item
      title="Monitor clusters using key metrics"
      body="Understand the different metrics in YugabyteDB to monitor your cluster"
      href="../../launch-and-manage/monitor-and-alert/metrics"
      icon="fa-solid fa-bell">}}
{{</index/block>}}
