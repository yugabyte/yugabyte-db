---
title: YugabyteDB Aeon Performance monitoring
headerTitle: Performance monitoring
linkTitle: Performance
description: Tools for monitoring YugabyteDB Aeon cluster performance.
headcontent: Evaluate cluster performance
menu:
  stable_yugabyte-cloud:
    identifier: monitor-performance
    parent: cloud-monitor
    weight: 100
aliases:
  - /stable/yugabyte-cloud/cloud-monitor/overview/
type: docs
---

Monitor performance metrics for your cluster to ensure the cluster configuration matches its performance requirements using the cluster **Performance** tab.

- **Metrics** displays a comprehensive array of more specific performance metrics, including for YSQL and YCQL API performance.
- **Queries>Slow Queries** shows YSQL queries run on the cluster, sorted by running time. Evaluate the slowest running YSQL queries that have been run on the cluster
- **Queries>Live Queries** tab shows the queries that are currently "in-flight" on your cluster.
- **Cluster Load** shows the load on the cluster at a glance. Use this view to answer the question _Was the system overloaded, and why_.
- **Insights** scans clusters for performance optimizations, including index and schema changes, and to detect potentially hot nodes.

Use these tools to monitor the performance of your cluster and to determine whether the configuration needs to change. For information on changing or scaling your cluster, refer to [Scale and configure clusters](../../cloud-clusters/configure-clusters/).

You can show metrics by region and by node, for the past hour, 6 hours, 12 hours, 24 hours, or 7 days.

## Overview metrics

The **Overview** tab displays a summary of the cluster infrastructure, along with time series charts of four key performance metrics for all the nodes in the cluster - Operations/sec, Average Latency, CPU Usage, and Disk Usage, averaged over all the nodes in the cluster.

You can enable alerts for CPU usage and disk usage. Refer to [Alerts](../cloud-alerts/).

The following table describes the metrics available on the **Overview**.

| Graph | Description | Use |
| :---| :--- | :--- |
| Operations/sec | The number of [YB-TServer](../../../architecture/yb-tserver/) read and write operations per second. | Spikes in read operations are normal during backups and scheduled maintenance. If the count drops significantly below average, it might indicate an application connection failure. If the count is much higher than average, it could indicate a DDoS, security incident, and so on. Coordinate with your application team because there could be legitimate reasons for dips and spikes. |
| Average Latency (ms) | Read: the average latency of read operations at the tablet level.<br>Write: the average latency of write operations at the tablet level. | When latency starts to degrade, performance may be impacted by the storage layer. |
| CPU Usage (%) | The percentage of CPU use being consumed by the tablet or master server Yugabyte processes, as well as other processes, if any. In general, CPU usage is a measure of all processes running on the server. | High CPU use could indicate a problem and may require debugging by Yugabyte Support. An [alert](../cloud-alerts/) is issued when node CPU use exceeds 70% (Warning) or 90% (Severe) on average for at least 5 minutes. |
| Disk Usage (GB) | Shows the amount of disk space provisioned for and used by the cluster. | Typically you would scale up at 80%, but consider this metric in the context of your environment. For example, usage can be higher on larger disks. An [alert](../cloud-alerts/) is issued when the free storage on any node in the cluster falls below 40% (Warning) and 25% (Severe). |
