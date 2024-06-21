---
title: Observability in YugabyteDB
headerTitle: Observability
linkTitle: Observability
description: Observability in YugabyteDB.
headcontent: Monitoring, alerting, and analyzing metrics
image: /images/section_icons/secure/authorization.png
aliases:
  - /preview/explore/observability-docker/macos
  - /preview/explore/observability-docker/linux
  - /preview/explore/observability-docker/docker
menu:
  preview:
    identifier: explore-observability
    parent: explore
    weight: 299
type: indexpage
showRightNav: true
---

Observability refers to the extent to which the internal state and behavior of a system can be understood, monitored, and analyzed from the outside, typically by developers and DevOps. It focuses on providing insight into how a system is performing, what is happening inside it, and how it is interacting with its environment.

The goal of observability is to make it easier to diagnose and resolve issues, optimize performance, and gain insights into the system's behavior. It is especially important in modern, complex, and distributed systems, where understanding the interactions between different services and components can be challenging. [DevOps Research and Assessment (DORA)](https://dora.dev/) research shows that a comprehensive monitoring and observability solution, along with several other technical practices, positively contributes to the management of software infrastructure.

YugabyteDB provides several components and features that you can use to actively monitor your system and diagnose issues quickly.

## Metrics

Use metrics to track trends and identify performance issues, and manage the system's performance and reliability.

YugabyteDB exports various [metrics](../../launch-and-manage/monitor-and-alert/metrics/#frequently-used-metrics), which are effectively quantitative measurements of the cluster's performance and behavior. These metrics include details on latency, connections, cache, consensus, replication, response times, resource usage, and more:

- [Throughput and latency metrics](../../launch-and-manage/monitor-and-alert/metrics/throughput)
- [Connection metrics](../../launch-and-manage/monitor-and-alert/metrics/connections)
- [Cache and storage subsystem metrics](../../launch-and-manage/monitor-and-alert/metrics/cache-storage)
- [Raft and distributed system metrics](../../launch-and-manage/monitor-and-alert/metrics/raft-dst)
- [Replication metrics](../../launch-and-manage/monitor-and-alert/metrics/replication)
- [YB-Master metrics](../../launch-and-manage/monitor-and-alert/metrics/ybmaster)

## Alerting and monitoring

Monitoring involves continuously checking the system's health and performance and notifying stakeholders if any issues arise. For this, you can set up automated alerts based on predefined thresholds or conditions. All metrics exposed by YugabyteDB are exportable to third-party monitoring tools like [Prometheus](./prometheus-integration/macos/) and [Grafana](./grafana-dashboard/grafana/) which provide industry-standard alerting functionalities.

{{<tip>}}
Both [YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/) and [YugabyteDB Managed](../../yugabyte-cloud/cloud-monitor/cloud-alerts/) provide a full suite of alerting capabilities for monitoring.
{{</tip>}}

## Visualization and analysis

YugabyteDB provides dashboards that include charts, graphs, and other visual representations of the system's state and performance. [yugabyted](../../reference/configuration/yugabyted/) starts a web-UI on port 15433 that displays different charts for various metrics.

You can also export the metrics provided by YugabyteDB onto third-party visualization tools like [Prometheus](./prometheus-integration/macos/) and [Grafana](./grafana-dashboard/grafana/) as per the needs of your organization.

{{<tip>}}
Both [YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/anywhere-metrics/) and [YugabyteDB Managed](../../yugabyte-cloud/cloud-monitor/overview/) come with a full suite of visualizations to help you monitor your cluster and troubleshoot issues.
{{</tip>}}

## Logging

Logs from different services, such as the [YB-TServer](../../troubleshoot/nodes/check-logs/#yb-tserver-logs) and [YB-Master](../../troubleshoot/nodes/check-logs/#yb-master-logs) provide a historical record of what has happened and can be very helpful in debugging and troubleshooting. These logs are rotated regularly, based on their size as configured. See [Logs management](../../troubleshoot/nodes/check-logs#logs-management).

## Query-level metrics

The following table describes views in YSQL you can use to monitor and tune query performance.

| View | Description |
| :--- | :---------- |
| [pg_stat_statements](../query-1-performance/pg-stat-statements) | Get query statistics (such as the _time spent by a query_) |
| [pg_stat_activity](./pg-stat-activity) | View and analyze live queries |
| [yb_local_tablets](./yb-local-tablets) | Get YSQL/YCQL and tablet metadata details |
| [yb_terminated_queries](./yb-pg-stat-get-queries/) | Identify terminated queries |
| [pg_stat_progress_copy](./pg-stat-progress-copy) | Get the status of a COPY command execution |
| [pg_locks](./pg-locks) | Get information on locks held by a transaction |

To get more details about the various steps of a query execution, use the [Explain Analyze](../query-1-performance/explain-analyze) command.

## Active Session History

[Active Session History](active-session-history/) (ASH) offers insight into current and past system activity by periodically sampling session behavior in the database. ASH functionality extends to [YSQL](../../api/ysql/), [YCQL](../../api/ycql/), and [YB-TServer](../../architecture/yb-tserver/) processes, and helps you to conduct analytical queries, perform aggregations, and troubleshoot performance issues.

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
      title="View live queries with pg_stat_activity"
      body="Troubleshoot problems and identify long-running queries with the activity view."
      href="./pg-stat-activity/"
      icon="/images/section_icons/manage/diagnostics.png">}}

  {{<index/item
      title="View YQL and tablet metadata with yb_local_tablets"
      body="See metadata about the YSQL and YCQL statements, and system tablets of a node."
      href="./yb-local-tablets/"
      icon="fa-solid fa-tablets">}}

  {{<index/item
      title="View terminated queries with yb_terminated_queries"
      body="Identify terminated queries with the get queries function."
      href="./yb-pg-stat-get-queries/"
      icon="/images/section_icons/explore/monitoring.png">}}

  {{<index/item
      title="View COPY status with pg_stat_progress_copy"
      body="Get the COPY command status, number of tuples processed, and other COPY progress reports with this view."
      href="./pg-stat-progress-copy/"
      icon="/images/section_icons/explore/json_documents.png">}}

  {{<index/item
      title="Get lock information insights with pg_locks"
      body="Get lock information about current transactions, diagnose and resolve any contention issues in YugabyteDB"
      href="./pg-locks/"
      icon="/images/section_icons/explore/secure.png">}}

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
