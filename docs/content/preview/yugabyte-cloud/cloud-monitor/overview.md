---
title: Performance metrics
linkTitle: Performance metrics
description: View time series charts of cluster metrics.
headcontent: Evaluate cluster performance with time series charts
image: /images/section_icons/deploy/enterprise.png
menu:
  preview_yugabyte-cloud:
    identifier: overview-clusters
    parent: cloud-monitor
    weight: 100
type: docs
---

Monitor performance metrics for your cluster to ensure the cluster configuration matches its performance requirements using the cluster **Overview** and **Performance Metrics** tabs.

- The **Overview** tab displays a summary of the cluster infrastructure, along with time series charts of four key performance metrics for all the nodes in the cluster - Operations/sec, Average Latency, CPU Usage, and Disk Usage.

- The **Performance** tab **Metrics** displays a comprehensive array of more specific performance metrics, including for YSQL and YCQL API performance.

Use these metrics to monitor the performance of your cluster and to determine whether the configuration needs to change. For information on changing or scaling your cluster, refer to [Scale and configure clusters](../../cloud-clusters/configure-clusters/).

You can enable alerts for some performance metrics. Refer to [Alerts](../cloud-alerts/).

![Cluster Performance Metrics](/images/yb-cloud/cloud-clusters-metrics.png)

You can show metrics by region and by node, for the past hour, 6 hours, 12 hours, 24 hours, or 7 days.

## Overview metrics

The **Overview** tab shows metrics averaged over all the nodes in the cluster.

You can enable alerts for CPU usage and disk usage. Refer to [Alerts](../cloud-alerts/).

The following table describes the metrics available on the **Overview**.

| Graph | Description | Use |
| :---| :--- | :--- |
| Operations/sec | The number of disk input / output read and write operations (IOPS) per second. | Large spikes usually indicate large compactions. Rarely, in cases of a spiky workload, this could indicate block cache misses.<br>Because random reads always hit disk, you should increase IOPS capacity for this type of workload. |
| Average Latency | Read: the average latency of read operations at the tablet level.<br>Write: the average latency of write operations at the tablet level. | When latency starts to degrade, performance may be impacted by the storage layer. |
| CPU Usage | The percentage of CPU use being consumed by the tablet or master server Yugabyte processes, as well as other processes, if any. In general, CPU usage is a measure of all processes running on the server. | High CPU use could indicate a problem and may require debugging by Yugabyte Support. |
| Disk Usage | Shows the amount of disk space provisioned for and used by the cluster. | Typically you would scale up at 80%, but consider this metric in the context of your environment. For example, usage can be higher on larger disks; some file systems issue an alert at 75% usage due to performance degradation. |

## Performance metrics

To choose the metrics to display on the **Performance** tab, click **Metrics** and then click **Options**. You can display up to four metrics at a time. You can additionally view the metrics for specific nodes.

The **Performance** tab provides the following metrics.

| Graph | Description | Use |
| :---| :--- | :--- |
| **Overall** | | |
| Operations/sec | The number of disk input / output read and write operations (IOPS) per second. | Large spikes usually indicate large compactions. Rarely, in cases of a spiky workload, this could indicate block cache misses.<br>Because random reads always hit disk, you should increase IOPS capacity for this type of workload. |
| Average Latency | Read: the average latency of read operations at the tablet level.<br>Write: the average latency of write operations at the tablet level. | When latency starts to degrade, performance may be impacted by the storage layer. |
| **General** | | |
| CPU Usage | The percentage of CPU use being consumed by the tablet or master server Yugabyte processes, as well as other processes, if any. In general, CPU usage is a measure of all processes running on the server. | High CPU use could indicate a problem and may require debugging by Yugabyte Support. |
| Disk Usage | Shows the amount of disk space provisioned for and used by the cluster. | Typically you would scale up at 80%, but consider this metric in the context of your environment. For example, usage can be higher on larger disks; some file systems issue an alert at 75% usage due to performance degradation. |
| Network Bytes / Sec | The size (in bytes; scale: millions) of network packets received (RX) and transmitted (TX) per second, averaged over nodes. | Provides a view of the intensity of the network activity on the server. |
| Disk Bytes / Sec | The number of bytes (scale: millions) being read or written to disk per second, averaged over each node. | If the maximum IOPS for the instance volume type has high utilization, you should ensure that the schema and query are optimized. In addition, consider increasing the instance volume IOPS capacity. |
| Network Errors | The number of errors related to network packets received (RX) and transmitted (TX) per second, averaged over nodes. | You should issue an alert for any error, unless the environment produces a lot of errors. |
| **Advanced** | | |
| YCQL Remote Operations | The number of remote read and write requests. Remote requests are re-routed internally to a different node for executing the operation. | If an application is using a Yugabyte driver that supports local query routing optimization and prepared statements, the expected value for this is close to zero. If using the Cassandra driver or not using prepared statements, expect to see a relatively even split between local and remote operations (for example, ~66% of requests to be remote for a 3-node cluster).
| RPC Queue Size | The number of remote procedure calls (RPC) in service queues for tablet servers, including the following services: CDC (Change Data Capture); Remote Bootstrap; TS RPC (Tablet Server Service); Consensus; Admin; Generic; Backup. | The queue size is an indicator of the incoming traffic. If the backends get overloaded, requests pile up in the queues. When the queue is full, the system responds with backpressure errors. |
| **YSQL** | | |
| YSQL Operations/Sec | The count of DELETE, INSERT, SELECT, and UPDATE statements through the YSQL API. This does not include index writes. | If the count drops significantly lower than your average count, it might indicate an application connection failure. In addition, if the count is much higher than your average count, it could indicate a DDoS, security incident, and so on. You should coordinate with your application team because there could be legitimate reasons for dips and spikes. |
| YSQL Latency | Average time (in milliseconds) of DELETE, INSERT, SELECT, and UPDATE statements through the YSQL API. | When latency is close to or higher than your application SLA, it may be a cause for concern. The overall latency metric is less helpful for troubleshooting specific queries. It is recommended that the application track query latency. There could be reasons your traffic experiences spikes in latency, such as when ad-hoc queries such as count(*) are executed. |
| **YCQL** | | |
| YCQL Operations/Sec | The count of DELETE, INSERT, SELECT, and UPDATE transactions, as well as other statements through the YCQL API. | If the count drops significantly lower than your average count, this could indicate an application connection failure. |
| YCQL Latency | The average time (in milliseconds) of DELETE, INSERT, SELECT, and UPDATE transactions, as well as other statements through the YCQL API. | When latency is close to or higher than your application SLA, it may be a cause for concern. |
| YCQL Latency (P99) | The average time (in milliseconds) of the top 99% of DELETE, INSERT, SELECT, and UPDATE transactions, as well as other statements through the YCQL API. | If this value is significantly higher than expected, then it might be a cause for concern. You should check whether or not there are consistent spikes in latency. |
| **Tablet Server** | | |
| WAL Bytes Written / Sec / Node | The number of bytes written to the write-ahead logging (WAL) after the tablet start. | A low-level metric related to the storage layer. This can help debug certain latency or throughput issues by isolating where the bottleneck happens. |
| **DocDB** | | |
| Compaction | The number of bytes being read and written to do compaction. | If not a lot of data is being deleted, these levels are similar. In some cases, you might see a large delete indicated by large reads but low writes afterwards (because a large percentage of data was removed in compaction). |
| Average SSTables / Node | The average number of SSTable (SST) files across nodes. | A low-level metric related to the storage layer. This can help debug certain latency or throughput issues by isolating where the bottleneck happens. |
