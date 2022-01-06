---
title: Scale and configure clusters
linkTitle: Scale and configure clusters
description: Scale Yugabyte Cloud clusters and configure IP allow lists for the cluster.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: configure-clusters
    parent: cloud-clusters
    weight: 200
isTocNested: true
showAsideToc: true
---

Yugabyte Cloud suppports both horizontal and vertical scaling of clusters. If your workloads have increased, you can dynamically add nodes to a running cluster to improve latency, throughput, and memory. Likewise, if your cluster is over-scaled, you can reduce nodes to reduce costs.

The **Infrastructure** section on the cluster **Settings** tab summarizes the cluster setup, including the region, number of nodes and vCPUs, the disk size, and fault tolerance.

You scale clusters using the **Edit Infrastructure** option, which is located on the **Settings** tab and in the **Quick Links** menu.

You can modify the number of nodes and vCPUs per node, and increase the disk size of clusters. The scaling operation is performed without any downtime, with a rolling restart of the underlying nodes.

{{< note title="Note" >}}

You cannot change the fault tolerance of a cluster once it is created. You cannot reduce disk size. You cannot scale Free clusters.

{{< /note >}}

## Scaling clusters

You cannot change the number of nodes or vCPUs per node in clusters with a fault tolerance of None. You can horizontally scale nodes in clusters with Node Level fault tolerance in increments of 1. Nodes in clusters with Availability Zone fault tolerance must be scaled in increments of 3.

Clusters include a minimum of 50GB of storage per vCPU. Disk size per node is adjusted automatically when you change the number of vCPUs. You can add additional storage by changing the disk size per node. To have more than 16 vCPUs per node, send your request to [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

To scale the cluster:

1. On the **Clusters** page, select your cluster.
1. On the **Settings** tab or under **Quick Links**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

    ![Cluster Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit.png)

1. Enter the number of nodes, vCPUs per node, and disk size in GB per node for the cluster.
    \
    **Cost** displays the estimated new cost for the cluster; **+ Usage** refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer. For information on how clusters are costed, refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

1. Click **Save** when you are done.

Depending on the number of nodes, the scaling operation can take several minutes or more, during which time some cloud operations will not be available.
