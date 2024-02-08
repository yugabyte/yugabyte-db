---
title: Observability in YugabyteDB
headerTitle: Observability
linkTitle: Observability
description: Observability in YugabyteDB.
headcontent: Monitoring, alerting, and analyzing metrics
image: /images/section_icons/secure/authorization.png
menu:
  stable:
    identifier: explore-observability
    parent: explore
    weight: 310
type: indexpage
showRightNav: true
---

Observability refers to the extent to which the internal state and behavior of a system can be understood, monitored, and analyzed from the outside, typically by developers and DevOps. It focuses on providing insight into how a system is performing, what is happening inside it, and how it is interacting with its environment.

The goal of observability is to make it easier to diagnose and resolve issues, optimize performance, and gain insights into the system's behavior. It is especially crucial in modern, complex, and distributed systems, where understanding the interactions between different services and components can be challenging. [DevOps Research and Assessment (DORA)](https://dora.dev/) research shows that a comprehensive monitoring and observability solution, along with several other technical practices, positively contributes to the management of software infrastructure.

YugabytedDB provides several components and features that you can use to actively monitor your system and diagnose issues quickly.

## Metrics

Use metrics to track trends and identify performance issues, and manage the system's performance and reliability.

YugabyteDB exports various [metrics](../../launch-and-manage/monitor-and-alert/metrics/#frequently-used-metrics), which are effectively quantitative measurements of the cluster's performance and behavior. These metrics include details on latency, connections, cache, consensus, replication, response times, resource usage, and more:

- [Throughput and latency metrics](../../launch-and-manage/monitor-and-alert/metrics/throughput)
- [Connection metrics](../../launch-and-manage/monitor-and-alert/metrics/connections)
- [Cache and storage metrics](../../launch-and-manage/monitor-and-alert/metrics/cache-storage)
- [Raft and distributed system metrics](../../launch-and-manage/monitor-and-alert/metrics/raft-dst)
- [Replication metrics](../../launch-and-manage/monitor-and-alert/metrics/replication)
- [YB-Master metrics](../../launch-and-manage/monitor-and-alert/metrics/ybmaster)

## Alerting and monitoring

Monitoring involves continuously checking the system's health and performance and notifying stakeholders if any issues arise. For this, you can set up automated alerts based on predefined thresholds or conditions. All metrics exposed by YugabytedDB are exportable to third-party monitoring tools like [Prometheus](./prometheus-integration/) and [Grafana](./grafana-dashboard/grafana/) which provide industry-standard alerting functionalities.

{{<note>}}
Both [YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/) and [YugabyteDB Managed](../../yugabyte-cloud/cloud-monitor/cloud-alerts/) provide a full suite of alerting capabilities for monitoring.
{{</note>}}

## Visualization and analysis

YugabytedB provides dashboards that include charts, graphs, and other visual representations of the system's state and performance. [yugabyted](../../reference/configuration/yugabyted/) starts a web-UI on port [15433](http://127.0.0.1:15433/performance/metrics?interval=lasthour&nodeName=all&showGraph=operations&showGraph=latency&showGraph=cpuUsage&showGraph=diskUsage&showGraph=totalLiveNodes) that displays different charts for various metrics.

You can also export the metrics provided by YugabyteDB onto third-party visualization tools like [Prometheus](./prometheus-integration/) and [Grafana](./grafana-dashboard/grafana/) as per the needs of your organization.

{{<note>}}
Both [YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/anywhere-metrics/) and [YugabyteDB Managed](../../yugabyte-cloud/cloud-monitor/overview/) come with a full suite of visualizations to help you monitor your cluster and troubleshoot issues.
{{</note>}}

## Logging

Logs from different services, such as the [YB-TServer](../../troubleshoot/nodes/check-logs/#yb-tserver-logs) and [YB-Master](../../troubleshoot/nodes/check-logs/#yb-master-logs) provide a historical record of what has happened and can be very helpful in debugging and troubleshooting. These logs are rotated regularly, based on their size as configured. See [Logs management](../../troubleshoot/nodes/check-logs#logs-management).

## Query-level metrics

View live queries using the [pg_stat_activity](../query-1-performance/pg-stat-activity) view, and get query statistics (such as the _time spent by a query_) using the [pg_stat_statements](../query-1-performance/pg-stat-statements) view. Use these query-level metrics to tune query performance.

To get more details about the various steps of a query execution, use the [Explain Analyze](../query-1-performance/explain-analyze) command.

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
