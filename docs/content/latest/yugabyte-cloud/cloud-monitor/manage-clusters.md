---
title: Cluster nodes
linkTitle: Cluster nodes
description: View Yugabyte Cloud cluster nodes.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: manage-clusters
    parent: cloud-monitor
    weight: 500
isTocNested: true
showAsideToc: true
---

Review the status of the nodes in your cluster using the **Nodes** tab. The **Nodes** tab lists the nodes in the cluster, including the name, cloud, RAM, SSTable (SST) size, and read and write operations per second, and the node status.

![Cloud Cluster Nodes tab](/images/yb-cloud/cloud-clusters-nodes.png)

Yugabyte Cloud suppports horizontal scaling of clusters. If your workloads have increased, you can dynamically add nodes to a running cluster to improve latency, throughput, and memory. Likewise, if your cluster is over-scaled, you can reduce nodes to reduce costs. For information on scaling clusters, refer to [Scale and configure clusters](../../cloud-clusters/configure-clusters/).
