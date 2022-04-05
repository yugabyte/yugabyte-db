---
title: Scale and configure clusters
linkTitle: Scale and configure clusters
description: Scale Yugabyte Cloud clusters and configure IP allow lists for the cluster.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  preview:
    identifier: configure-clusters
    parent: cloud-clusters
    weight: 100
isTocNested: true
showAsideToc: true
---

Yugabyte Cloud supports both horizontal and vertical scaling of clusters. If your workloads have increased, you can dynamically add nodes to a running cluster to improve latency, throughput, and memory. Likewise, if your cluster is over-scaled, you can reduce nodes to reduce costs.

You can scale the following cluster properties:

- Number of nodes (horizontal).
- Number of vCPUs per node (vertical).
- Disk size per node.

For clusters with Node level and Availability zone level fault tolerance, the scaling operation is performed without any downtime, with a rolling restart of the underlying nodes.

The **Infrastructure** section on the cluster **Settings** tab summarizes the cluster configuration, including the region, number of nodes and vCPUs, total disk size, and fault tolerance.

## Recommendations

- Most production applications require 4 to 8 vCPUs per node. Scale up smaller instance sizes; when the total number of vCPUs for your cluster exceeds 16, consider scaling out. For example, if you have a 3-node cluster with 2 vCPUs per node, scale up to 8 vCPUs per node before adding nodes.

- Adding or removing nodes incurs a load on the cluster. Perform scaling operations when the cluster isn't experiencing heavy traffic. Scaling during times of heavy traffic can temporarily degrade application performance and increase the length of time of the scaling operation.

- Before removing nodes from a cluster, make sure the reduced disk space will be sufficient for the existing and anticipated data.

## Limitations

- You can horizontally scale nodes in clusters with Node level fault tolerance in increments of 1. Nodes in clusters with Availability zone level fault tolerance are scaled in increments of 3.

- You can configure up to 16 vCPUs per node. To have more than 16 vCPUs per node, send your request to {{<support-cloud>}}.

- To avoid data loss, you can only increase disk size per node; once increased, you can't reduce it.

- You can't change the fault tolerance of a cluster after it is created.

- You can't scale single node clusters (fault tolerance none), you can only increase disk size.

- You can't scale free clusters.

## Scale a cluster

To scale a cluster:

1. On the **Clusters** page, select your cluster.
1. On the **Settings** tab or under **More Links**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

    ![Cluster Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit.png)

1. Enter the number of nodes, vCPUs per node, and disk size in GB per node for the cluster.
    \
    **Cost** displays the estimated new cost for the cluster; **+ Usage** refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer. For information on how clusters are costed, refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

1. Click **Save** when you are done.

Depending on the number of nodes, the scaling operation can take several minutes or more, during which time some cloud operations will not be available.
