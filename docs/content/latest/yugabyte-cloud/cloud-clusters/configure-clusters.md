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
    weight: 700
isTocNested: true
showAsideToc: true
---

The **Settings** tab provides a single point of access to scale and configure network access for the cluster.

Yugabyte Cloud suppports both horizontal and vertical scaling of paid clusters. If your workloads have increased, you can dynamically add nodes to a running cluster to improve latency, throughput, and memory. Likewise, if your cluster is over-scaled, you can reduce nodes to reduce costs. You cannot change the fault tolerance of a cluster once it is created.

![Cloud Cluster Settings page](/images/yb-cloud/cloud-clusters-settings.png)

## General

Displays the cluster name.

## Infrastructure

Summarizes the cluster setup, including the region, number of nodes and vCPUs, the disk size, and fault tolerance. 

You can modify the number of nodes, vCPUs, and increase the disk size of paid clusters. You cannot reduce disk size. The scaling operation is performed without any downtime, with a rolling restart of the underlying nodes.

To scale the cluster:

1. Click **Edit Infrastructure** to display the **Edit Infrastructure** dialog.
1. Enter the number of nodes, vCPUs, and disk size in GB for the cluster.
    \
    **Cost** displays the estimated new cost for the cluster; **+ Usage** refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer. For information on how clusters are costed, refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

1. Click **Save** when you are done.

## Network Access

The Network Access section provides the cluster connection parameters and a summary of network connections and IP allow lists.

For information on managing cloud network access, refer to [Configure Networking](../../cloud-network/).

### Connection Parameters

The cluster host address and port numbers for the YSQL and YCQL client APIs.

### VPC Peering

Lists the [VPC peers](../../cloud-network/vpc-peers/) assigned to paid clusters. VPC peers must be assigned when the cluster is created.
<!--
To add a connection:

1. Click **Add Connection** to display the **Remote Connections** sheet.
1. Select the **Private Endpoints** tab to display the private endpoints configured for your cloud.
1. To assign endpoints to the cluster, choose the **Select from list** option and select the endpoints you want to allow to access the cluster.
1. To create an endpoint, choose the **Create new Pivate Endpoint** option and enter the endpoint details. For information on endpoint configuration, refer to [Endpoints](../../cloud-network/endpoints/).
1. Select the **VPC Peers** tab to display the peers configured for your cloud.
1. To assign peers to the cluster, choose the **Select from list** option and select the peers you want to allow to access the cluster.
1. To create a peer, choose the **Create new VPC Peer** option and enter the peer details. For information on VPC peer configuration, refer to [VPC Peers](../../cloud-network/vpc-peers/).
1. Click **Save** when you are done.
-->

### IP Allow Lists

Lists the IP allow lists assigned to the cluster.

To assign a list to the cluster:

1. Click **Edit List** to display the **Add IP Allow List** sheet. The sheet lists the IP allow lists that have been configured for your cloud.
1. To include an IP allow list in the cluster, select it in the list.
1. To remove an IP allow list from the cluster, deselect it in the list.
1. To create a new list:
    - Click **Create New List and Add to Cluster**.
    - Enter a name and description for the list.
    - Enter the IP addresses and ranges to want to include in the list. Click **Detect and add my IP to this list** to add the IP address of your computer.
1. Click **Save** when you are done.

To manage your cloud IP allow lists, refer to [Manage IP Allow Lists](../../cloud-network/ip-whitelists/).

<!--
## Database Users

Lists the users assigned to the cluster.

To manage users for your cloud, refer to [Manage Cloud Access](../../cloud-admin/manage-access/).

To modify the users assigned to the cluster:

1. Click **Edit Users**.

## Database Security

- Edit Security
-->
