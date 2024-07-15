---
title: Partition by region
linkTitle: Partition by region
description: Deploy geo-partitioned clusters in YugabyteDB Aeon.
headcontent: Use geo-partitioning to pin data to regions
menu:
  preview_yugabyte-cloud:
    identifier: create-clusters-geopartition
    parent: create-clusters
    weight: 70
type: docs
---

Use [partition-by-region](../../create-clusters-topology/#partition-by-region) clusters to geo-locate data in specific regions.

Clusters consist of a primary region and any number of additional secondary regions, where the partitioned, region-specific data resides. You can add or remove regions as required. When first deploying, you can deploy a single cluster in the primary region.

{{< youtube id="9ESTXEa9QZY" title="Create a geo-partitioned cluster in YugabyteDB Aeon" >}}

## Tablespaces

You place data in regions of the cluster using tablespaces. Tables that don't belong to any tablespace are stored in the primary region.

YugabyteDB Aeon automatically creates tablespaces in the regions of your cluster named `region_name_ts`. For example, if you add the us-central1 region, the tablespace is named `us_central1_ts`.

To view your cluster tablespaces, you can enter the following command:

```sql
SELECT * FROM pg_tablespace;
```

Note that data pinned to a single region via tablespaces is not replicated to other regions, and remains subject to the fault tolerance of the cluster (Node- or Availability Zone-level).

For more information on specifying data placement for tables and indexes, refer to [Tablespaces](../../../../explore/going-beyond-sql/tablespaces/).

## Features

Partition-by-region clusters include the following features:

- Multi node clusters with availability zone- or node-level fault tolerance.
- No limit on cluster size - choose any cluster size based on your use case.
- Size each region to its load - add extra horsepower in high-traffic regions, and provision lower-traffic regions with fewer nodes.
- Horizontal and vertical scaling - add or remove nodes and vCPUs, and add storage to suit your production loads.
- VPC networking required.
- Automated and on-demand backups.
- Available in all [regions](../../create-clusters-overview/#cloud-provider-regions).
- Enterprise support.

## Prerequisites

- Partition-by-region clusters must be deployed in a VPC. Create a VPC for each region where you want to deploy the nodes in the cluster. Refer to [VPC network overview](../../cloud-vpcs/cloud-vpc-intro/).
- By default, clusters deployed in VPCs do not expose any publicly-accessible IP addresses. Unless you enable [Public Access](../../../cloud-secure-clusters/add-connections/), you can only connect from resources inside the VPC network. Refer to [VPC network overview](../../cloud-vpcs/).

## Create a partition-by-region cluster

To create a partition-by-region cluster, on the **Clusters** page, click **Add Cluster**, and choose **Dedicated** to start the **Create Cluster** wizard.

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

![Add Cluster Wizard - Partition by region data distribution](/images/yb-cloud/cloud-addcluster-partition-data.png)

Set **Data Distribution** to **Partition by region**.

Select a **Fault Tolerance** for the regions. [Fault tolerance](../../create-clusters-overview/#fault-tolerance) determines how resilient each region is to node and zone outages (planned or unplanned). Choose one of the following:

| Fault tolerance | Description | Scaling |
| :--- | :--- | :--- |
| **Zone** | Resilient to a single zone outage. Minimum of 3 nodes spread across 3 availability zones. This configuration provides the maximum protection for a data center outage. Recommended for production deployments. | Nodes are scaled in increments of 3 (each zone has the same number of nodes). |
| **Node** | Resilient to 1, 2, or 3 node outages, with a minimum of 3, 5, or 7 nodes respectively, deployed in a single availability zone. Not resilient to zone outages. | Nodes are scaled in increments of 1. |
| **None** | Minimum of 1 node, with no replication or resiliency. Recommended for development and testing only. | Nodes are scaled in increments of 1. |

Fault tolerance is applied to all regions in the cluster, including those added after cluster creation.

#### Select regions and node size

![Add Cluster Wizard - Primary region and size](/images/yb-cloud/cloud-addcluster-partition.png)

**Regions** - For each region, choose the following:

- [region](../../create-clusters-overview/#cloud-provider-regions) where the nodes will be located.
- VPC in which to deploy the nodes. Only VPCs using the selected cloud provider and available in the selected region are listed. For multi-region GCP clusters, the same VPC is used for all regions. VPCs must be created before deploying the cluster. Refer to [VPC networking](../../cloud-vpcs/).
- number of nodes to deploy in the region.
- number of virtual CPUs per node.
- disk size per node.
- disk input output (I/O) operations per second (IOPS) per node (AWS only).

To add additional regions to the cluster, click **Add Region**.

Partiton-by-region clusters support both horizontal and vertical scaling; you can add regions and change the cluster configuration after the cluster is created. Refer to [Scale and configure clusters](../../../cloud-clusters/configure-clusters/#partition-by-region-cluster).

Monthly costs for the cluster are estimated automatically.

{{% includeMarkdown "network-access.md" %}}

{{% includeMarkdown "include-security-settings.md" %}}

### Database Credentials

The database admin credentials are required to connect to the YugabyteDB database that is installed on the cluster.

You can use the default credentials generated by YugabyteDB Aeon, or add your own.

For security reasons, the database admin user does not have YSQL superuser privileges, but does have sufficient privileges for most tasks. For more information on database roles and privileges in YugabyteDB Aeon, refer to [Database authorization in YugabyteDB Aeon clusters](../../../cloud-secure-clusters/cloud-users/).

After the cluster is provisioned, you can [add more users and change your password](../../../cloud-secure-clusters/add-users/).

![Add Cluster Wizard - Database credentials](/images/yb-cloud/cloud-addcluster-admin.png)

Download the credentials, and click **Create Cluster**.

{{< warning title="Important" >}}

Save your database credentials. If you lose them, you won't be able to use the database.

{{< /warning >}}

After you complete the wizard, the **Clusters** page appears, showing the provisioning of your new cluster in progress.

When the cluster is ready, the cluster [Overview](../../../cloud-monitor/overview/) tab is displayed.

You now have a fully configured YugabyteDB cluster provisioned in YugabyteDB Aeon with the database admin credentials you specified.

## Next steps

- [Connect to your cluster](../../../cloud-connect/)
- [Add database users](../../../cloud-secure-clusters/add-users/)
- [Build an application](../../../../tutorials/build-apps/)
- [Scale clusters](../../../cloud-clusters/configure-clusters/#partition-by-region-cluster)
