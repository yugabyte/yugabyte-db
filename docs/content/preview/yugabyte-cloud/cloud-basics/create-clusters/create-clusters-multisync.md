---
title: Replicate across regions
linkTitle: Replicate across regions
description: Deploy multi-region synchronous clusters in YugabyteDB Managed.
headcontent: Deploy region-level fault tolerant clusters
menu:
  preview_yugabyte-cloud:
    identifier: create-clusters-multisync
    parent: create-clusters
    weight: 60
type: docs
---

Clusters [replicated across regions](../../create-clusters-topology/#replicate-across-regions) include a minimum of 3 nodes across 3 regions, providing region-level [fault tolerance](../../create-clusters-overview/#fault-tolerance). You can add or remove nodes in increments of 1 per region (each region has the same number of nodes).

{{< youtube id="fCjTB8IuTuA" title="Create a multi-region cluster in YugabyteDB Managed" >}}

## Preferred region

You can optionally designate one region in the cluster as preferred. The preferred region handles all read and write requests from clients.

Designating one region as preferred can reduce the number of network hops needed to process requests. For lower latencies and best performance, set the region closest to your application as preferred. If your application uses a smart driver, set the [topology keys](../../../../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing) to target the preferred region.

When no region is preferred, YugabyteDB Managed distributes requests equally across regions. You can set or change the preferred region after cluster creation.

Regardless of the preferred region setting, data is replicated across all the regions in the cluster to ensure region-level fault tolerance.

You can enable [follower reads](../../../../explore/going-beyond-sql/follower-reads-ysql/) to serve reads from non-preferred regions.

In cases where the cluster has read replicas and a client connects to a read replica, reads are served from the replica; writes continue to be handled by the preferred region.

## Features

Multi-region replicated clusters include the following features:

- Replicated synchronously across 3 to 7 regions.
- No limit on cluster size - choose any cluster size based on your use case.
- Horizontal and vertical scaling - add or remove nodes and vCPUs, and add storage to suit your production loads.
- VPC networking required.
- Automated and on-demand backups.
- Available in all [regions](../../create-clusters-overview/#cloud-provider-regions).
- Enterprise support.

## Prerequisites

- Multi-region clusters must be deployed in VPCs. Create a VPC for each region where you want to deploy the nodes in the cluster. Refer to [VPC network overview](../../cloud-vpcs/cloud-vpc-intro/).
- By default, clusters deployed in VPCs do not expose any publicly-accessible IP addresses. Unless you enable [Public Access](../../../cloud-secure-clusters/add-connections/), you can only connect from resources inside the VPC network. Refer to [VPC network overview](../../cloud-vpcs/).
- A billing profile and payment method. Refer to [Manage your billing profile and payment method](../../../cloud-admin/cloud-billing-profile/).

## Create a multi-region replicated cluster

To create a multi-region replicated cluster, on the **Clusters** page, click **Add Cluster**, and choose **Dedicated** to start the **Create Cluster** wizard.

The **Create Cluster** wizard has the following pages:

1. [General Settings](#general-settings)
1. [Cluster Setup](#cluster-setup)
1. [Network Access](#network-access)
1. [Security](#security)
1. [DB Credentials](#database-credentials)

{{% includeMarkdown "include-general-settings.md" %}}

### Cluster Setup

Select **Multi-Region Deployment** and set the following options.

#### Select data distribution mode

![Add Cluster Wizard - Multi-region data distribution](/images/yb-cloud/cloud-addcluster-multisync-data.png)

Set **Data Distribution** to **Replicate across regions**.

Select the [Fault tolerance](../../create-clusters-overview/#fault-tolerance) for the cluster, as follows:

- Resilient to 1 region outage; requires a minimum of 3 nodes across 3 regions.
- Resilient to 2 region outages; requires a minimum of 5 nodes across 5 regions.
- Resilient to 3 region outages; requires a minimum of 7 nodes across 7 regions.

Clusters can be scaled in increments of 1 node per region; for example, a cluster with fault tolerance of 2 regions can be scaled in multiples of 5 nodes, one per region.

#### Select regions and node size

![Add Cluster Wizard - Multi-region and size](/images/yb-cloud/cloud-addcluster-multisync.png)

**Regions**: For each region, choose the following:

- the [region](../../create-clusters-overview/#cloud-provider-regions) where the nodes will be located.
- the VPC in which to deploy the nodes. Only VPCs using the selected cloud provider and available in the selected region are listed. For AWS clusters, choose a separate VPC for each region. For GCP clusters, the same VPC is used for all regions. VPCs must be created before deploying the cluster. Refer to [VPC networking](../../cloud-vpcs/).
- The number of nodes to deploy in the regions. Each region has the same number of nodes.

**Preferred region**: Optionally, assign one region as [preferred](#preferred-region) to handle all reads and writes.

**Node size**: Enter the number of virtual CPUs per node, disk size per node (in GB), and disk input output (I/O) operations per second (IOPS) per node (AWS only). You must choose the regions before you can set the node size.

The node throughput will be scaled according to the IOPS value. For large datasets or clusters with high concurrent transactions, higher IOPS is recommended. As disk IOPS is capped by vCPU, your vCPU and IOPS should be scaled together. Reference your current read and write IOPS performance for an estimation.

Clusters replicated across regions support both horizontal and vertical scaling; you can change the cluster configuration and preferred region after the cluster is created. Refer to [Scale and configure clusters](../../../cloud-clusters/configure-clusters/#replicate-across-regions-clusters).

Monthly costs for the cluster are estimated automatically.

{{% includeMarkdown "network-access.md" %}}

{{% includeMarkdown "include-security-settings.md" %}}

### Database Credentials

The database admin credentials are required to connect to the YugabyteDB database that is installed on the cluster.

You can use the default credentials generated by YugabyteDB Managed, or add your own.

For security reasons, the database admin user does not have YSQL superuser privileges, but does have sufficient privileges for most tasks. For more information on database roles and privileges in YugabyteDB Managed, refer to [Database authorization in YugabyteDB Managed clusters](../../../cloud-secure-clusters/cloud-users/).

After the cluster is provisioned, you can [add more users and change your password](../../../cloud-secure-clusters/add-users/).

![Add Cluster Wizard - Database credentials](/images/yb-cloud/cloud-addcluster-admin.png)

Download the credentials, and click **Create Cluster**.

{{< warning title="Important" >}}

Save your database credentials. If you lose them, you won't be able to use the database.

{{< /warning >}}

After you complete the wizard, the **Clusters** page appears, showing the provisioning of your new cluster in progress.

When the cluster is ready, the cluster [Overview](../../../cloud-monitor/overview/) tab is displayed.

You now have a fully configured YugabyteDB cluster provisioned in YugabyteDB Managed with the database admin credentials you specified.

## Next steps

- [Connect to your cluster](../../../cloud-connect/)
- [Add database users](../../../cloud-secure-clusters/add-users/)
- [Build an application](../../../../tutorials/build-apps/)
- [Scale clusters](../../../cloud-clusters/configure-clusters/#replicate-across-regions-clusters)
