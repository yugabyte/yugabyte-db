---
title: Performance metrics
linkTitle: Performance metrics
description: View time series charts of cluster metrics.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: overview-clusters
    parent: cloud-monitor
    weight: 100
isTocNested: true
showAsideToc: true
---

Monitor key performance metrics for your cluster to ensure the cluster configuration matches its performance requirements using the cluster **Overview** and **Performance Metrics** tabs.

- The **Overview** tab displays a summary of the cluster infrastructure, along with time series charts of performance metrics for all the nodes in the cluster.

- The **Performance** tab **Metrics** display time series charts of performance metrics for the nodes in the cluster; you can view metrics for all the nodes in the cluster, and for individual nodes.

You can show metrics for the past hour, 6 hours, 12 hours, 24 hours, or 7 days. On the **Performance** tab you can further view metrics for specific nodes.

![Cloud Cluster Performance Metrics](/images/yb-cloud/cloud-clusters-metrics.png)

The following **Key Metrics** are tracked on the **Overview** and **Performance** tabs:

- Operations/sec - Shows the read and write operations on the cluster over time.
- Average Latency - Shows the average amount of time in milliseconds taken for read and write operations.
- CPU Usage - Shows the percentage of CPU use for the cluster.
- Disk Usage - Shows the amount of disk space provisioned for and used by the cluster.

To change or scale your cluster, refer to [Scale and configure clusters](../../cloud-clusters/configure-clusters/).
