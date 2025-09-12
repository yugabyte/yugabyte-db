---
title: Scale and configure clusters
linkTitle: Scale and configure clusters
description: Scale YugabyteDB Aeon clusters.
headcontent: Scale clusters horizontally and vertically
menu:
  preview_yugabyte-cloud:
    identifier: configure-clusters
    parent: cloud-clusters
    weight: 100
type: docs
---

YugabyteDB Aeon supports both horizontal and vertical scaling of clusters. If your workloads have increased, you can dynamically add nodes to a running cluster to improve latency, throughput, and memory. Likewise, if your cluster is over-scaled, you can reduce nodes to reduce costs.

You can scale the following cluster properties:

- Number of nodes (horizontal).
- Number of vCPUs per node (vertical).
- Disk size per node.
- IOPS (AWS clusters only)

Cluster edit operations are performed using the **Edit Infrastructure** option under **Regions** on the cluster **Settings > Infrastructure** tab.

{{< youtube id="Dhb-R-tlFKM" title="Perform a live infrastructure upgrade in YugabyteDB Aeon" >}}

For clusters with Region, Availability Zone, or Node Level fault tolerance, the scaling operation is performed without any downtime, with a rolling restart of the underlying nodes.

The **Regions** section on the cluster **Settings > Infrastructure** tab summarizes the cluster configuration, including the number of nodes, vCPUs, memory, and disk per node, and VPC for each region.

## Recommendations

- Most production applications require 4 to 8 vCPUs per node. Scale up smaller instance sizes; when the total number of vCPUs for your cluster exceeds 16, consider scaling out. For example, if you have a 3-node cluster with 2 vCPUs per node, scale up to 8 vCPUs per node before adding nodes.
- Adding or removing nodes incurs a load on the cluster. Depending on the amount of data in your database, horizontal scaling can also take time, as adding or removing nodes requires moving data between nodes. Perform scaling operations when the cluster isn't experiencing heavy traffic. Scaling during times of heavy traffic can temporarily degrade application performance and increase the length of time of the scaling operation.
- Scaling operations [lock the cluster](../#locking-operations), blocking other cluster operations, such as backups and maintenance. Avoid scaling operations before maintenance windows and during scheduled backups. The operation will block a backup from running.
- Before removing nodes from a cluster, make sure the reduced disk space will be sufficient for the existing and anticipated data.

## Limitations

- You can horizontally scale nodes in clusters with Node Level [fault tolerance](../../cloud-basics/create-clusters-overview/#fault-tolerance) in increments of 1. Nodes in clusters with Availability Zone Level fault tolerance are scaled in increments of 3 (1 per zone). Nodes in clusters with Region Level fault tolerance are scaled in increments of 1 per region.
- You can configure up to 16 vCPUs per node. To have more than 16 vCPUs per node, send your request to {{% support-cloud %}}.
- To avoid data loss, you can only increase disk size per node; once increased, you can't reduce it.
- Currently, you can't change the cluster fault tolerance using the YugabyteDB Aeon UI; you can change it using the [API](../../managed-automation/managed-api/) or [CLI](../../managed-automation/managed-cli/).
- You can't change vCPUs on clusters with fault tolerance of none, you can only increase disk size, or change IOPS (AWS only).
- Changing disk size or IOPS on a cluster with fault tolerance of none results in downtime.
- You can't scale Sandbox clusters.
- If another [locking cluster operation](../#locking-operations) is already running, you must wait for it to finish.
- Some scaling operations require a rolling restart or, in the case of clusters with a fault tolerance of none, downtime.
- On AWS, you can't make further modifications to disk (size, IOPS) for six hours after changing either disk size or IOPS (this includes a scaling operation that increases the number of vCPUs, as this also increases disk size).

## Scale and configure clusters

### Single-region clusters

You can scale multi-node single-region clusters horizontally and vertically, as well as increase the disk size.

To add or remove read replicas, refer to [Read replicas](../managed-read-replica/).

For Availability Zone Level fault tolerant clusters, you must scale nodes in increments of 3 (all zones must have the same number of nodes).

To scale a single-region cluster:

1. On the **Clusters** page, select your cluster.
1. Go to **Settings > Infrastructure** or click **Actions**, and choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

    ![Cluster Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit.png)

1. Enter the number of nodes, vCPUs per node, disk size in GB per node, and disk input output (I/O) operations per second (IOPS) per node (AWS only) for the cluster.

    Monthly costs for the cluster are estimated automatically.

1. Click **Confirm and Save Changes** when you are done.

Depending on the number of nodes, whether you are adding or removing nodes, and the amount of data in your database, a horizontal scaling operation can take minutes or hours, during which time some cluster operations will not be available.

### Replicate-across-regions clusters

You can scale multi-region replicated clusters horizontally and vertically, as well as increase the disk size.

<!--In addition, you can migrate nodes to different regions; migrated nodes can be deployed to different VPCs.-->

To add or remove read replicas, refer to [Read replicas](../managed-read-replica/).

To scale nodes in a replicate-across-regions cluster:

1. On the **Clusters** page, select your cluster.
1. Go to **Settings > Infrastructure** or click **Actions**, and choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

    ![Cluster Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit-sync.png)

    <!--1. To migrate nodes to a different region, select the region. When migrating a node, you can also deploy it in a different VPN.-->

1. Enter the number of nodes, vCPUs per node, disk size in GB per node, and disk input output (I/O) operations per second (IOPS) per node (AWS only) for the cluster.

    The same number of nodes and node sizes apply across all regions.

    Monthly costs for the cluster are estimated automatically.

1. Optionally, set or change the [Preferred region](../../cloud-basics/create-clusters/create-clusters-multisync/#preferred-region).

1. Click **Confirm and Save Changes** when you are done.

Depending on the number of nodes, the scaling operation can take several minutes or more, during which time some cluster operations will not be available.

### Partition-by-region cluster

You can scale partition-by-region clusters horizontally and vertically.

In addition, you can add and delete regions.

New regions must be deployed in a VPC. New regions have the same fault tolerance as the primary cluster. YugabyteDB Aeon automatically creates tablespaces in the new regions named `region_name_ts`. For example, if you add the us-central1 region, the tablespace is named `us_central1_ts`.

Before you can delete a region, you must drop all the tablespaces located in the region. You can't delete the primary region.

For Availability Zone Level fault tolerant clusters, you must scale nodes in each region in increments of 3.

To scale a partition-by-region cluster:

1. On the **Clusters** page, select your cluster.
1. Go to **Settings > Infrastructure** or click **Actions**, and choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

    ![Cluster Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit-geo.png)

1. To add a region, click **Add Region**, choose the region, select the VPC where you want to deploy the cluster, and enter the number of nodes. The new region has the same fault tolerance as the primary cluster.

1. To scale the cluster horizontally, enter the number of nodes for each region.

1. To scale the cluster vertically, enter the number of vCPUs per node, disk size in GB per node, and disk input output (I/O) operations per second (IOPS) per node (AWS only) for each region.

    Monthly costs for the cluster are estimated automatically.

1. Click **Confirm and Save Changes** when you are done.

Depending on the number of nodes, the scaling operation can take several minutes or more, during which time some cluster operations will not be available.
