---
title: Alerts and monitoring in YugabyteDB Aeon
headerTitle: Alerts and monitoring
linkTitle: Alerts and monitoring
description: Set alerts and monitor your YugabyteDB Aeon clusters.
headcontent: Set alerts and monitor cluster performance and activity
aliases:
  - /stable/yugabyte-cloud/cloud-monitor/logging-export/
menu:
  stable_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-monitor
    weight: 150
    params:
      classes: separator
      hideLink: true
type: indexpage
showRightNav: true
---

{{< page-finder/head text="Monitor YugabyteDB" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../launch-and-manage/monitor-and-alert" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../yugabyte-platform/alerts-monitoring/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" current="" >}}
{{< /page-finder/head >}}

### Alerts

Use YugabyteDB Aeon [Alerts](cloud-alerts/) to be automatically notified of potential problems. You can enable alerts for cluster, database, and billing criteria. Configure alerts from the **Alerts** page.

### Performance monitoring

Monitor database and cluster performance in real time. Access performance monitoring from the cluster **Perf Advisor** tab.

| Feature | Description |
| :--- | :--- |
| [Metrics](overview/) | The cluster **Overview** and **Performance Metrics** tabs show a variety of performance metrics charted over time. Use cluster performance metrics to ensure the cluster configuration matches its performance requirements, and [scale the cluster vertically or horizontally](../cloud-clusters/configure-clusters/) as your requirements change. |
| [Live queries](cloud-queries-live/) | The cluster **Live Queries** tab shows the queries that are currently "in-flight" on your cluster. |
| [Slow queries](cloud-queries-slow/) | The cluster **YSQL Slow  Queries** tab shows queries run on the cluster, sorted by running time. Evaluate the slowest running YSQL queries that have been run on the cluster. |
| [Insights](cloud-advisor/) | Scan clusters for performance optimizations, including index and schema changes, and detect potentially hot nodes. |

### Integrations

Integrate with third-party tools such as Datadog, Grafana Cloud, and Sumo Logic to export database metrics and logs for analysis.

| Feature | Description |
| :--- | :--- |
| [Integrations](managed-integrations/) | Set up links to third-party monitoring tools. |
| [Metrics export](metrics-export/) | Export metrics to third-party monitoring tools. |
| [Log export](logging-export/) | Export database PostgreSQL query and audit logs to third-party monitoring tools. |

### Tables, nodes, and cluster activity

View cluster tables and their tablets, nodes and their status, and audit cluster activity.

| Feature | Description |
| :--- | :--- |
| [Tables and Tablets](monitor-tables/) | Use the cluster **Tables** tab to view the cluster tables and their sizes. Select a table to view its tablets. |
| [Nodes](monitor-nodes/) | Use the cluster **Nodes** tab to see the nodes in the cluster, their status, and a variety of performance metrics. |
| [Activity log](monitor-activity/) | The cluster **Activity** tab provides a running audit of changes made to the cluster. |

### System status

To view an uptime report for your account (Enterprise plan only) and the YugabyteDB Aeon system status, click the **System Status** icon in the bottom left corner.
