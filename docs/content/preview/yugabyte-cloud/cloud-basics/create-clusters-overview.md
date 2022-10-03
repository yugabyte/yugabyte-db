---
title: Plan your cluster
linkTitle: Plan your cluster
description: Plan a cluster in YugabyteDB Managed.
headcontent: Before deploying a production cluster, consider the following factors
image: /images/section_icons/deploy/enterprise.png
menu:
  preview_yugabyte-cloud:
    identifier: create-clusters-overview
    parent: cloud-basics
    weight: 10
type: docs
---

## Best practices

The following best practices are recommended for production clusters.

| Feature | Recommendation |
| :--- | :--- |
| [Provider and region](#provider-and-region) | Deploy your cluster in a virtual private cloud (VPC), with the same provider and in the same region as your application VPC. YugabyteDB Managed supports AWS and GCP.<br>Multi-region clusters must be deployed in VPCs. You need to create the VPCs before you deploy the cluster. Refer to [VPC network](../cloud-vpcs/). |
| [Fault tolerance](#fault-tolerance) | Region or Availability zone (AZ) level - minimum of three nodes across multiple regions or AZs, with a replication factor of 3. |
| [Sizing](#sizing) | For most production applications, at least 3 nodes with 4 to 8 vCPUs per node.<br>Clusters support 10 simultaneous connections per vCPU. For example, a 3-node cluster with 4 vCPUs per node can support 10 x 3 x 4 = 120 connections.<br>When scaling your cluster, for best results increase node size up to 16 vCPUs before adding more nodes. For example, for a 3-node cluster with 4 vCPUs per node, scale up to 8 or 16 vCPUs before adding a fourth node. |
| [YugabyteDB version](#yugabytedb-version) | Use the **Stable** release track.<!--<br>Use a [staging cluster](#staging-clusters) to test upgrades before upgrading your production cluster.--> |
| [Backups](#backups) | Use the default backup schedule (daily, with 8 day retention). |
| [Security and authorization](#security) | YugabyteDB Managed clusters are secure by default. After deploying, set up IP allow lists and add database users to allow clients, applications, and application VPCs to connect. Refer to [IP allow lists](../../cloud-secure-clusters/add-connections/). |

## In depth

### Topology

A YugabyteDB cluster typically consists of three or more nodes that communicate with each other and across which data is distributed. You can place the nodes in a single availability zone, across multiple zones in a single region, and across regions. With more advanced topologies, you can place multiple clusters across multiple regions. The [topology](../create-clusters-topology/) you choose depends on your requirements for latency, availability, and geo-distribution.

#### Single Region

Single-region clusters are available in the following topologies:

- **Single availability zone**. Resilient to node outages.
- **Multiple availability zones**. Resilient to node and availability zone outages.

Cloud providers like AWS and Google Cloud design zones to minimize the risk of correlated failures caused by physical infrastructure outages like power, cooling, or networking. In other words, single failure events usually affect only a single zone. By deploying nodes across zones in a region, you get resilience to a zone failure as well as high availability.

Single-region clusters are not resilient to region-level outages.

#### Multiple Region

Multi-region clusters are resilient to region-level outages, and are available in the following topologies:

- **Replicate across regions**. Cluster nodes are deployed across 3 regions, with data replicated synchronously.
- **Partition by region**. Cluster nodes are deployed in separate regions. Data is pinned to specific geographic regions. Allows fine-grained control over pinning rows in a user table to specific geographic locations.
- **Cross-cluster**. Two clusters are deployed in separate regions. Data is shared between the clusters, either in one direction, or asynchronously.
- **Read replica**. Multiple clusters are deployed in separate regions. Data is written in a single region, and copied to the other regions, where it can be read. The primary cluster gets all write requests, while read requests can go either to the primary cluster or to the read replica clusters depending on which is closest.

Multi-region clusters must be deployed in VPCs, with each region or read replica deployed in its own VPC. Refer to [VPC networking](../cloud-vpcs/).

For more details, refer to [Topologies](../create-clusters-topology/).

### Provider and region

#### Provider

YugabyteDB Managed supports AWS and GCP. Your choice of provider will depend primarily on where your existing applications are hosted. YugabyteDB Managed pricing is the same for both.

#### Region

For best performance as well as lower data transfer costs, you want to minimize transfers between providers, and between provider regions. Do this by locating your cluster as close to your applications as possible:

- Use the same cloud provider as your application.
- Locate your cluster in the same region as your application.

For lowest possible network latency and data transfer costs, deploy your cluster in a VPC on the same cloud provider as your application VPC and peer it with the application VPC. This configuration also provides the best security.

For a list of supported regions, refer to [Cloud provider regions](../../release-notes/#cloud-provider-regions).

### Fault tolerance

The _fault tolerance_ determines how resilient the cluster is to node and cloud zone failures. YugabyteDB Managed provides the following options for providing replication and redundancy:

- **Region level**. Includes 3 nodes spread across multiple regions with a RF of 3. YugabyteDB can continue to do reads and writes even in case of a cloud region failure. This configuration provides the maximum protection for a regional failure.

- **Availability zone level**. Includes a minimum of 3 nodes spread across multiple availability zones with a [replication factor](../../../architecture/docdb-replication/replication/) (RF) of 3. YugabyteDB can continue to do reads and writes even in case of a cloud availability zone failure. This configuration provides the maximum protection for a data center failure.

- **Node level**. Includes a minimum of 3 nodes deployed in a single availability zone with a RF of 3. YugabyteDB can continue to do reads and writes even in case of a node failure, but this configuration is not resilient to cloud availability zone outages.

Although you can't change the cluster fault tolerance after the cluster is created, you can scale horizontally as follows:

- For Region level, you can add or remove nodes in increments of 1 per region; all regions have the same number of nodes.
- For Availability zone level, you can add or remove nodes in increments of 3.
- For Node level, you can add or remove nodes in increments of 1.

For production clusters, a minimum of Availability zone level is recommended. Whether you choose Region or Availability zone level depends on your application architecture, design, and latency requirements.

For application development and testing, you can set fault tolerance to **None** to create a single-node cluster. Single-node clusters can't be scaled.

### Sizing

The size of the cluster is based on the number of vCPUs. The default configuration for YugabyteDB Managed clusters includes 4 vCPUs per node. Each vCPU comes with 50GB of storage. A node has a minimum of 2 vCPUs with 4GB of memory per vCPU. For the cluster to be [fault tolerant](#fault-tolerance), you need a minimum of 3 nodes.

YugabyteDB Managed clusters support 10 simultaneous connections per vCPU. So a cluster with 3 nodes and 4 vCPUs per node can support 10 x 3 x 4 = 120 simultaneous connections.

| Cluster size (node x vCPU) | Maximum simultaneous connections |
| :--- | :--- |
| 3x2 | 60 |
| 3x4 | 120 |
| 3x8 | 240 |
| 6x2 | 120 |
| 6x4 | 240 |
| 6x8 | 360 |

During an update, one node is always offline. When sizing your cluster to your workload, ensure you have enough additional capacity to support rolling updates with minimal impact on application performance. You can also mitigate the effect of updates on performance by [scheduling them](../../cloud-clusters/cloud-maintenance/) during periods of lower traffic.

YugabyteDB Managed supports both vertical and horizontal scaling. Depending on your performance requirements, you can increase the number of vCPUs per node, as well as the total number of nodes. You can also increase the disk size per node. However, once increased, you can't lower the disk size per node.

If your configuration doesn't match your performance requirements, you can change these values after the cluster is created (increasing or decreasing vCPUs and increasing storage, and adding or removing nodes). Refer to [Scaling clusters](../../cloud-clusters/configure-clusters/).

For production clusters, a minimum of 3 nodes with 4 to 8 vCPUs per node is recommended.

### YugabyteDB version

By default, clusters are created using a stable release, taken from the [stable release series](../../../releases/versioning/#stable-releases) of YugabyteDB.

You can choose to deploy your cluster using a preview release for development and testing. YugabyteDB Managed database preview releases are typically taken from the [preview release series](../../../releases/versioning/#preview-releases) of YugabyteDB, though they can also include a recently released stable release.

If you need a feature from a preview release (that isn't yet available in a stable release) for a production deployment, contact {{% support-cloud %}} before you create your cluster.

Yugabyte manages upgrades for you. After you choose a track, database upgrades continue to take releases from the track you chose. For multi-node clusters, Yugabyte performs a rolling upgrade without any downtime. You can manage when Yugabyte performs maintenance and upgrades by configuring the [maintenance window](../../cloud-clusters/cloud-maintenance/) for your cluster.

<!-- #### Staging clusters

Yugabyte tests every version in the stable branch for backwards compatibility. However, it's good practice to first test database updates against your pre-production environment (aka development, testing, staging, or canary environment) to ensure compatibility before upgrading your production clusters.

Create a staging cluster (this can be smaller than your production cluster) and configure your pre-production environment to connect to it. When you are notified of an upcoming maintenance event, schedule the [maintenance windows](../../cloud-clusters/cloud-maintenance/) for the staging and production cluster so that you can validate updates against your applications in your pre-production environment before updating your production cluster.
-->

### Backups

YugabyteDB Managed provides a default recommended backup schedule (daily, with 8 day retention), and manages backups for you. You can [change the default schedule](../../cloud-clusters/backup-clusters/#schedule-backups), as well as perform [on-demand backups](../../cloud-clusters/backup-clusters/#on-demand-backups).

YugabyteDB Managed performs full cluster (all namespaces) level backups, and the backups are stored in the same region as your cluster. 100GB/month of basic backup storage is provided for every vCPU; more than that and overage charges apply. Refer to [Backup storage costs](../../cloud-admin/cloud-billing-costs/#backup-storage-costs).

### Security

Clusters are secure by default. You need to explicitly allow access to clusters by adding IP addresses of clients connecting to the cluster to the cluster IP allow list. Refer to [IP allow lists](../../cloud-secure-clusters/add-connections/).

If your applications are running in a VPC, deploy your cluster in a VPC to improve security and lower network latency. You also need to add the CIDR ranges of any application VPCs to your cluster IP allow list.

Multi-region clusters must be deployed in VPCs, with each region or read replica deployed in its own VPC.

You need to create VPCs before you deploy the cluster. YugabyteDB Managed supports AWS and GCP for VPCs. Refer to [VPC network](../cloud-vpcs/).

#### User authorization

YugabyteDB uses role-based access control to manage database access. When you create a cluster, YugabyteDB Managed adds a default admin user (the credentials for this user are configurable).

After the cluster is provisioned, create a new database and [add users](../../cloud-secure-clusters/add-users/). You can create users specific to each connecting application, and restrict their access accordingly.

{{< note title="Note" >}}
In YSQL, the admin user is not a full superuser. For security reasons, you do not have access to the Yugabyte or Postgres superusers, nor can you create users with superuser privileges.
{{< /note >}}

For more information on users and roles in YugabyteDB Managed, refer to [Database authorization in YugabyteDB Managed clusters](../../cloud-secure-clusters/cloud-users/).

## Pricing

The biggest factor in the price of a cluster is the number vCPUs.

Cluster charges are based on the total number of vCPUs used and how long they have been running. Cluster per-hour charges include [free allowances](../../cloud-admin/cloud-billing-costs/) for disk storage, backup storage, and data transfer. If you use more than the free allowance, you incur overages on top of the base vCPU capacity cost.

Before creating a cluster, you need to create your billing profile and add a payment method. Refer to [Manage your billing profile and payment method](../../cloud-admin/cloud-billing-profile/).

If you're interested in evaluating YugabyteDB Managed for production use and would like trial credits to conduct a proof-of-concept (POC), contact {{% support-cloud %}}.

## Next steps

- [Create a cluster](../create-clusters/)
