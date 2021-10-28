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

Monitor key performance metrics for your cluster to ensure the cluster configuration matches its performance requirements using the **Overview** tab. The **Overview** tab displays information about the cluster, along with time series charts of performance metrics.

![Cluster Overview tab](/images/yb-cloud/cloud-clusters-overview.png)

The following **Key Metrics** are tracked on the **Overview** tab:

- Operations/sec - Shows the read and write operations on the cluster over time.
- Average Latency - Shows the average amount of time in milliseconds taken for read and write operations.
- CPU Usage - Shows the percentage of CPU use for the cluster.
- Disk Usage - Shows the amount of disk space provisioned for and used by the cluster.

You can show metrics for the past hour, 6 hours, 12 hours, 24 hours, or 7 days.

To change or scale your cluster, refer to [Scale and configure clusters](../configure-clusters/).
