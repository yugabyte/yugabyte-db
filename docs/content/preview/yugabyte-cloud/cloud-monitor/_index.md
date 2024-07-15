---
title: Alerts and monitoring in YugabyteDB Aeon
headerTitle: Alerts and monitoring
linkTitle: Alerts and monitoring
description: Set alerts and monitor your YugabyteDB Aeon clusters.
image: /images/section_icons/explore/monitoring.png
headcontent: Set alerts and monitor cluster performance and activity
aliases:
  - /preview/yugabyte-cloud/cloud-monitor/logging-export/
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-monitor
    weight: 100
type: indexpage
showRightNav: true
---

### Alerts

Use YugabyteDB Aeon [Alerts](cloud-alerts/) to be automatically notified of potential problems. You can enable alerts for cluster, database, and billing criteria. Configure alerts from the **Alerts** page.

### Performance monitoring

Monitor database and cluster performance in real time. Access performance monitoring from the cluster **Performance** tab.

| Feature | Description |
| :--- | :--- |
| [Metrics](overview/) | The cluster **Overview** and **Performance Metrics** tabs show a variety of performance metrics charted over time. Use cluster performance metrics to ensure the cluster configuration matches its performance requirements, and [scale the cluster vertically or horizontally](../cloud-clusters/configure-clusters/) as your requirements change. |
| [Live queries](cloud-queries-live/) | The cluster **Live Queries** tab shows the queries that are currently "in-flight" on your cluster. |
| [Slow queries](cloud-queries-slow/) | The cluster **YSQL Slow  Queries** tab shows queries run on the cluster, sorted by running time. Evaluate the slowest running YSQL queries that have been run on the cluster. |
| [Performance&nbsp;advisor](cloud-advisor/) | Scan clusters for performance optimizations, including index and schema changes, and detect potentially hot nodes. |

### Data analysis

Export database metrics and logs to third-party tools for analysis.

| Feature | Description |
| :--- | :--- |
| [Metrics export](metrics-export/) | Export metrics to third-party monitoring tools such as Datadog, Grafana Cloud, and Sumo Logic. |
| [Log export](logging-export/) | Export database PostgreSQL logs to third-party monitoring tools. |

### Cluster properties

View cluster activity, node status, and database properties.

| Feature | Description |
| :--- | :--- |
| Database&nbsp;tables | Use the cluster **Tables** tab to see the cluster tables, and their database or namespace, and size. Note that table size is calculated from the sum of the write ahead logs (WAL) and sorted-string table (SST) files, across all nodes in the cluster. Changes to the database are first recorded to the WAL. Periodically, these logs are written to SST files for longer-term storage. During this process, the data is compressed. When this happens, you may observe a reduction in the total size of tables. |
| Node status | Use the cluster **Nodes** tab to see the nodes in the cluster and their status. |
| [Activity log](monitor-activity/) | The cluster **Activity** tab provides a running audit of changes made to the cluster. |

{{<index/block>}}

  {{<index/item
    title="Alerts"
    body="Enable alerts for cluster performance metrics and billing."
    href="cloud-alerts/"
    icon="/images/section_icons/explore/monitoring.png">}}

  {{<index/item
    title="View performance metrics"
    body="Evaluate cluster performance with time series charts of performance metrics."
    href="overview/"
    icon="/images/section_icons/explore/high_performance.png">}}

  {{<index/item
    title="View live queries"
    body="Monitor and display current running queries on your cluster."
    href="cloud-queries-live/"
    icon="/images/section_icons/explore/monitoring.png">}}

  {{<index/item
    title="View slow queries"
    body="Monitor and display past YSQL queries on your cluster."
    href="cloud-queries-slow/"
    icon="/images/section_icons/explore/monitoring.png">}}

  {{<index/item
    title="Performance advisor"
    body="Scan your database for potential optimizations."
    href="cloud-advisor/"
    icon="/images/section_icons/manage/diagnostics.png">}}

  {{<index/item
    title="Export metrics"
    body="Export cluster metrics to third-party monitoring tools."
    href="metrics-export/"
    icon="/images/section_icons/explore/monitoring.png">}}

  {{<index/item
    title="Export logs"
    body="Export cluster logs to third-party logging tools."
    href="logging-export/"
    icon="/images/section_icons/explore/monitoring.png">}}

  {{<index/item
    title="Monitor cluster activity"
    body="Review the activity on your cluster."
    href="monitor-activity/"
    icon="/images/section_icons/explore/monitoring.png">}}

{{</index/block>}}
