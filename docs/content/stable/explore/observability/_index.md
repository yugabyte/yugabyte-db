---
title: Observability in YugabyteDB
headerTitle: Observability
linkTitle: Observability
description: Observability in YugabyteDB.
headcontent: Monitoring, alerting, and analyzing metrics
aliases:
  - /stable/explore/observability-docker/macos
  - /stable/explore/observability-docker/linux
  - /stable/explore/observability-docker/docker
menu:
  stable:
    identifier: explore-observability
    parent: explore
    weight: 299
type: indexpage
showRightNav: true
---

Observability refers to the extent to which the internal state and behavior of a system can be understood, monitored, and analyzed from the outside, typically by developers and DevOps. It focuses on providing insight into how a system is performing, what is happening inside it, and how it is interacting with its environment.

Observability has a wide variety of use cases.

{{%table%}}

| Use case | Description |
| -------- | ----------- |

| Operational monitoring | Build an application health dashboard for your critical applications using key operational signals that are constantly monitored. Add alerts for DevOps or SRE teams so they can act quickly in case of an event to ensure business continuity. The application health dashboard collects signals, metrics from YugabyteDB, and other systems that power your application, such as APIs, web app, SDK, and so on. |
| Performance troubleshooting | Database administrators and application developers need to be able to troubleshoot issues, perform root cause analysis, and issue fixes. You can create a dashboard to monitor an observed issue causing temporal, gradual, or systemic performance degradation, or application failure. To conduct root cause analysis, issue-dependent deep observability metrics in a specific area are typically used. These metrics are consumed at the time of root cause analysis and operating teams fall back to a health dashboard after the issue is identified, the fix is monitored, and the issue is resolved. |
| Object monitoring | Monitor specific parts of application behavior continuously after a new feature launch, during maintenance windows, during application upgrades, and more. The metrics can be system-wide or specific to the object of interest, such as a YugabyteDB cluster, node, tablet, geography, users, tenant, and more. |

{{%/table%}}

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

Monitoring involves continuously checking the system's health and performance and notifying stakeholders if any issues arise. For this, you can set up automated alerts based on predefined thresholds or conditions. All metrics exposed by YugabyteDB are exportable to third-party monitoring tools like [Prometheus](./prometheus-integration/) and [Grafana](./grafana-dashboard/grafana/) which provide industry-standard alerting functionalities.

{{<tip>}}
Both [YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/) and [YugabyteDB Aeon](../../yugabyte-cloud/cloud-monitor/cloud-alerts/) provide a full suite of alerting capabilities for monitoring.
{{</tip>}}

## Visualization and analysis

YugabyteDB provides dashboards that include charts, graphs, and other visual representations of the system's state and performance. [yugabyted](../../reference/configuration/yugabyted/) starts a web-UI on port 15433 that displays different charts for various metrics.

You can also export the metrics provided by YugabyteDB onto third-party visualization tools like [Prometheus](./prometheus-integration/) and [Grafana](./grafana-dashboard/grafana/) as per the needs of your organization.

{{<tip>}}
Both [YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/anywhere-metrics/) and [YugabyteDB Aeon](../../yugabyte-cloud/cloud-monitor/overview/) come with a full suite of visualizations to help you monitor your cluster and troubleshoot issues.
{{</tip>}}

## Query-level statistics

The pg_stat_statements extension tracks and aggregates statistics for SQL queries executed on the database. It helps monitor query performance by recording execution counts, total and average execution times, rows processed, and resource usage (for example, shared buffer hits and disk I/O). The extension groups queries with similar structures (normalized queries) to provide a concise and meaningful view of query behavior.

By analyzing the pg_stat_statements view, database administrators can identify slow, frequently executed, or resource-intensive queries, making it a powerful tool for performance tuning. It is straightforward to use — enable the extension and query the view to gain insights into query patterns and optimize database performance.

{{<lead link="../../launch-and-manage/monitor-and-alert/query-tuning/pg-stat-statements">}}
To get more info on query level statistics, see [pg_stat_statements](../../launch-and-manage/monitor-and-alert/query-tuning/pg-stat-statements)
{{</lead>}}

## Live queries

The pg_stat_activity system view provides real-time information about the currently active database sessions. Use it to monitor user connections, query execution, and session states to understand database activity, and diagnose performance issues.

{{<lead link="./pg-stat-activity">}}
To understand how to view live queries, see [pg_stat_activity](./pg-stat-activity)
{{</lead>}}

## Tablet information

YugabyteDB provides two views for accessing tablet metadata:

- **yb_local_tablets**: Provides information about tablets on a specific node/server. This view returns the same information that is available on `<yb-tserver-ip>:9000/tablets`.

- **yb_tablet_metadata**: Provides cluster-wide tablet distribution and leadership information, including replica locations and leader nodes. This view serves as the YSQL equivalent of the YCQL `system.partitions` table.

{{<lead link="./yb-local-tablets">}}
To view tablet metadata for a specific node, see [yb_local_tablets](./yb-local-tablets)
{{</lead>}}

{{<lead link="./yb-tablet-metadata">}}
To view cluster-wide tablet distribution and leadership, see [yb_tablet_metadata](./yb-tablet-metadata)
{{</lead>}}

## Terminated queries

Queries may be terminated by the system due to a variety reasons not including server crash, resource limitations, misbehaviour.

{{<lead link="./yb-pg-stat-get-queries">}}
To view which queries have been terminated for what reasons, see [yb_terminated_queries](./yb-pg-stat-get-queries/)
{{</lead>}}

## Copy status

Use the COPY command to transfer data in and out of a database. This could be a long running operation depending on the size of data.

{{<lead link="./pg-stat-progress-copy">}}
To view the progress of the COPY command, see [pg_stat_progress_copy](./pg-stat-progress-copy)
{{</lead>}}

## Lock information

The pg_locks view in PostgreSQL provides information about locks currently held or awaited by database sessions. Locks are crucial for maintaining data consistency and ensuring proper concurrency control in a multi-user environment. This view helps database administrators understand the locking behavior of queries and detect potential issues like deadlocks or contention.

By querying pg_locks, you can identify which processes are holding locks, waiting for locks, and the types of locks involved (for example, row-level, table-level). This information is invaluable for diagnosing performance bottlenecks caused by lock contention, optimizing query execution, and ensuring smooth database operation.

{{<lead link="./pg-locks">}}
To understand how to view and use lock information, see [pg_locks](./pg-locks)
{{</lead>}}

## Active Session History

Active Session History (ASH) offers insight into current and past system activity by periodically sampling session behavior in the database. ASH functionality extends to [YSQL](../../api/ysql/), [YCQL](../../api/ycql/), and [YB-TServer](../../architecture/yb-tserver/) processes, and helps you to conduct analytical queries, perform aggregations, and troubleshoot performance issues.

{{<lead link="./active-session-history">}}
To learn more, see [Active Session History](./active-session-history)
{{</lead>}}

## Logging

Logs provide a crucial record of events across numerous interconnected components and are indispensable for debugging and troubleshooting, allowing engineers to trace errors and understand the complex interactions between services. By aggregating and analyzing logs, teams can monitor system health, identify performance bottlenecks, and gain valuable insights into system behavior. Logs provide the essential observability required to manage the complexity of distributed environments, enabling efficient problem-solving and ensuring system reliability.

{{<lead link="./logging/">}}
To understand the logging system in YugabyteDB, see [Logging](./logging/)
{{</lead>}}
