---
title: Scale and configure clusters
linkTitle: Scale and configure clusters
description: Scale YugabyteDB Managed clusters.
headcontent: Scale clusters horizontally and vertically
image: /images/section_icons/deploy/enterprise.png
menu:
  preview_yugabyte-cloud:
    identifier: configure-clusters
    parent: cloud-clusters
    weight: 100
type: docs
---

YugabyteDB Managed supports both horizontal and vertical scaling of clusters. If your workloads have increased, you can dynamically add nodes to a running cluster to improve latency, throughput, and memory. Likewise, if your cluster is over-scaled, you can reduce nodes to reduce costs.

{{< youtube id="yL4WR6wpjPs" title="Perform a live infrastructure upgrade in YugabyteDB Managed" >}}

You can scale the following cluster properties:

- Number of nodes (horizontal).
- Number of vCPUs per node (vertical).
- Disk size per node.

For clusters with Node level and Availability zone level fault tolerance, the scaling operation is performed without any downtime, with a rolling restart of the underlying nodes.

The **Regions** section on the cluster **Settings** tab summarizes the cluster configuration, including the number of nodes, vCPUs, memory, and disk per node, and VPC for each region.

## Recommendations

- Most production applications require 4 to 8 vCPUs per node. Scale up smaller instance sizes; when the total number of vCPUs for your cluster exceeds 16, consider scaling out. For example, if you have a 3-node cluster with 2 vCPUs per node, scale up to 8 vCPUs per node before adding nodes.

- Adding or removing nodes incurs a load on the cluster. Perform scaling operations when the cluster isn't experiencing heavy traffic. Scaling during times of heavy traffic can temporarily degrade application performance and increase the length of time of the scaling operation.

- Before removing nodes from a cluster, make sure the reduced disk space will be sufficient for the existing and anticipated data.

## Limitations

- You can horizontally scale nodes in clusters with Node level fault tolerance in increments of 1. Nodes in clusters with Availability zone level fault tolerance are scaled in increments of 3.

- You can configure up to 16 vCPUs per node. To have more than 16 vCPUs per node, send your request to {{% support-cloud %}}.

- To avoid data loss, you can only increase disk size per node; once increased, you can't reduce it.

- You can't change the fault tolerance of a cluster after it is created.

- You can't scale single node clusters (fault tolerance none), you can only increase disk size.

- You can't scale Sandbox clusters.

## Scale and configure clusters

### Single-region clusters

You can scale multi-node single-region clusters horizontally and vertically.

To scale a single-region cluster:

1. On the **Clusters** page, select your cluster.
1. On the **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

    ![Cluster Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit.png)

1. Enter the number of nodes, vCPUs per node, and disk size in GB per node for the cluster.
    \
    **Cost** displays the estimated new cost for the cluster; **+ Usage** refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer. For information on how clusters are costed, refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

1. Click **Confirm and Save Changes** when you are done.

Depending on the number of nodes, the scaling operation can take several minutes or more, during which time some cluster operations will not be available.

### Replicate across regions clusters

You can scale multi-region replicated clusters horizontally and vertically. <!--In addition, you can migrate nodes to different regions; migrated nodes can be deployed to different VPCs.-->

To scale nodes in a multi-region replicated cluster:

1. On the **Clusters** page, select your cluster.
1. On the **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

    ![Cluster Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit-sync.png)

    <!--1. To migrate nodes to a different region, select the region. When migrating a node, you can also deploy it in a different VPN.-->

1. Enter the number of nodes, vCPUs per node, and disk size in GB per node for the cluster. The same number of nodes and node sizes apply across all regions.
    \
    **Cost** displays the estimated new cost for the cluster; **+ Usage** refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer. For information on how clusters are costed, refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

1. Click **Confirm and Save Changes** when you are done.

Depending on the number of nodes, the scaling operation can take several minutes or more, during which time some cluster operations will not be available.

<!--### Partition by region cluster

You can scale geo-partitioned clusters horizontally and vertically. In addition, you can add new regions; these must be deployed in a VPC. New regions have the same fault tolerance as the primary cluster.

To scale a multi-region geo-partioned cluster:

1. On the **Clusters** page, select your cluster.
1. On the **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

    ![Cluster Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit-geo.png)

1. To add a region, click **Add Region**, choose the region, select the VPC where you want to deploy the cluster, and enter the number of nodes. The new region has the same fault tolerance as the primary cluster.

1. To scale the cluster, enter the number of nodes, vCPUs per node, and disk size in GB per node. The same number of nodes and node sizes apply across all regions.
    \
    **Cost** displays the estimated new cost for the cluster; **+ Usage** refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer. For information on how clusters are costed, refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

1. Click **Confirm and Save Changes** when you are done.

Depending on the number of nodes, the scaling operation can take several minutes or more, during which time some cluster operations will not be available.-->
