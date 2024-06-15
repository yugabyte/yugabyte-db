---
title: Plan your cluster
linkTitle: Plan your cluster
description: Plan a cluster in YugabyteDB Managed.
headcontent: Before deploying a production cluster, consider the following factors
menu:
  preview_yugabyte-cloud:
    identifier: create-clusters-overview
    parent: cloud-basics
    weight: 10
type: docs
---

## Summary of best practices

The following best practices are recommended for production clusters.

| Feature | Recommendation |
| :--- | :--- |
| [Provider and region](#provider-and-region) | Deploy your cluster in a virtual private cloud (VPC), with the same provider and in the same region as your application VPC. YugabyteDB Managed supports AWS, Azure, and GCP.<br>Multi-region clusters must be deployed in VPCs. You need to create the VPCs before you deploy the cluster. Refer to [VPC network](../cloud-vpcs/). |
| [Fault tolerance](#fault-tolerance) | Region or Availability zone (AZ) level - minimum of three nodes across multiple regions or AZs. |
| [Sizing](#sizing) | For most production applications, at least 3 nodes with 4 to 8 vCPUs per node.<br>Clusters support 15 simultaneous connections per vCPU. For example, a 3-node cluster with 4 vCPUs per node can support 15 x 3 x 4 = 180 connections. |
| [YugabyteDB version](#yugabytedb-version) | Use the **Production** release track. |
| [Staging cluster](#staging-cluster) | Use a staging cluster to test application compatibility with database updates before upgrading your production cluster. |
| [Backups](#backups) | Use the default backup schedule (daily, with 8 day retention). |
| [Security and authorization](#security) | YugabyteDB Managed clusters are secure by default. Deploy clusters in a VPC and configure either peering or a private link. Refer to [VPC network](../cloud-vpcs/). After deploying, set up IP allow lists and add database users to allow clients, applications, and application VPCs to connect. Refer to [Connect to clusters](../../cloud-connect/). |

## In depth

### Topology

A YugabyteDB cluster typically consists of three or more nodes that communicate with each other and across which data is distributed. You can place the nodes in a single availability zone, across multiple zones in a single region, and across regions. With more advanced topologies, you can place multiple clusters across multiple regions. The [topology](../create-clusters-topology/) you choose depends on your requirements for latency, availability, and geo-distribution.

#### Single Region

Single-region clusters are available in the following topologies and fault tolerance:

- **Single availability zone**. Resilient to node outages.
- **Multiple availability zones**. Resilient to node and availability zone outages.

Cloud providers design zones to minimize the risk of correlated failures caused by physical infrastructure outages like power, cooling, or networking. In other words, single failure events usually affect only a single zone. By deploying nodes across zones in a region, you get resilience to a zone outage as well as high availability.

Single-region clusters are not resilient to region-level outages.

#### Multiple Region

Multi-region clusters are resilient to region-level outages, and are available in the following topologies:

- **Replicate across regions**. Cluster nodes are deployed across 3 regions, with data replicated synchronously.
- **Partition by region**. Cluster nodes are deployed in separate regions. Data is pinned to specific geographic regions. Allows fine-grained control over pinning rows in a user table to specific geographic locations.
- **Read replica**. Replica clusters are added to an existing primary cluster and deployed in separate regions, typically remote from the primary. Data is written in the primary cluster, and copied to the read replicas, where it can be read. The primary cluster still gets all write requests, while read requests can go either to the primary cluster or to the read replica clusters depending on which is closest.
<!-- - **Cross-cluster**. Two clusters are deployed in separate regions. Data is shared between the clusters, either in one direction, or asynchronously. -->

Multi-region clusters must be deployed in VPCs, with each region and read replica deployed in a VPC. Refer to [VPC networking](../cloud-vpcs/).

For more details, refer to [Topologies](../create-clusters-topology/).

### Provider and region

#### Provider

YugabyteDB Managed supports AWS, Azure, and GCP. Your choice of provider will depend primarily on where your existing applications are hosted. YugabyteDB Managed pricing is the same for all.

| Feature | AWS | Azure | GCP |
| :--- | :--- | :--- | :--- |
| Sandbox | Yes | No | Yes |
| VPC | Yes | Yes (Required) | Yes |
| Peering | Yes | No | Yes |
| Private Service Endpoint | Yes | Yes | No |
| Topologies | Single region<br/>Replicate across regions<br/>Partition by region | Single region<br/>Replicate across regions<br/>Partition by region | Single region<br/>Replicate across regions<br/>Partition by region |
| Read replicas | Yes | Yes | Yes |
| Customer Managed Key | Yes | No | Yes |

#### Region

For best performance as well as lower data transfer costs, you want to minimize transfers between providers, and between provider regions. Do this by locating your cluster as close to your applications as possible:

- Use the same cloud provider as your application.
- Locate your cluster in the same region as your application.

For lowest possible network latency and data transfer costs, deploy your cluster in a VPC on the same cloud provider as your application VPC. This configuration also provides the best security. You can connect your cluster to the application VPC in the following ways:

- [Peering](../cloud-vpcs/cloud-add-peering/) [AWS or GCP]. For best results, your application should be located in one of the regions where your cluster is deployed.
- [Private link](../cloud-vpcs/cloud-add-endpoint/) [AWS or Azure]. To connect using a private link, the link endpoints (your cluster and the application VPC) must be in the same region.

For a list of supported regions, refer to [Cloud provider regions](#cloud-provider-regions).

#### Instance types

An instance in cloud computing is a server resource provided by third-party cloud services. An instance abstracts physical computing infrastructure using virtual machines. It's similar to having your own server machine in the cloud.

Cloud providers offer a variety of instance types across the regions where they have data centers. When creating clusters, YugabyteDB Managed chooses the most suitable type for the cloud provider, subject to what is available in the selected regions, and uses the same instance type for all nodes in the cluster.

### Fault tolerance

YugabyteDB achieves resiliency by replicating data across fault domains using the [Raft consensus protocol](../../../architecture/docdb-replication/replication/). The fault domain can be at the level of individual nodes, availability zones, or entire regions.

The _fault tolerance_ determines how resilient the cluster is to domain (that is, node, zone, or region) outages, whether planned or unplanned. Fault tolerance is achieved by adding redundancy, in the form of additional nodes, across the fault domain. Due to the way the Raft protocol works, providing a fault tolerance of `ft` requires replicating data across `2ft + 1` domains. For example, to survive the outage of 2 nodes, a cluster needs 2 * 2 + 1 nodes. While the 2 nodes are offline, the remaining 3 nodes can continue to serve reads and writes without interruption.

With a fault tolerant cluster, planned outages such as maintenance and upgrades are performed using a rolling restart, meaning your workloads are not interrupted.

YugabyteDB Managed provides the following configurations for fault tolerance.

| Fault tolerance | Resilient to | Minimum number of nodes | Scale in increments of |
| :-------------- | :----------- | :---------------------: | :--------------------: |
| **Node**        | 1 Node outage    | 3 | 1 |
|                 | 2 Node outages   | 5 | 1 |
|                 | 3 Node outages   | 7 | 1 |
| **Zone**        | 1 Zone outage    | 3 across 3 zones   | 3 |
| **Region**      | 1 Region outage  | 3 across 3 regions | 3 |
|                 | 2 Region outages | 5 across 5 regions | 5 |
|                 | 3 Region outages | 7 across 7 regions | 7 |

You can't change the cluster fault tolerance after the cluster is created.

For production clusters, a minimum of Availability Zone Level is recommended. Whether you choose Region or Availability Zone Level depends on your application architecture, design, and latency requirements.

For application development and testing, you can set fault tolerance to **None** to create a single-node cluster. A cluster with fault tolerance of None (including your Sandbox) is subject to downtime during maintenance and outages.

#### Region

- YugabyteDB can continue to do reads and writes even in case of a cloud region outage.
- Minimum of 3 nodes across 3 regions, 5 nodes across 5 regions, or 7 nodes across 7 regions.
- Add or remove nodes in increments of 1 per region; all regions have the same number of nodes. For example, for a fault tolerance of 2 regions, you must scale in increments of 5 (one node per region).

#### Availability Zone

- YugabyteDB can continue to do reads and writes even in case of a cloud availability zone outage.
- Minimum of 3 nodes across 3 availability zones for a fault tolerance of 1 zone.
- Because cloud providers typically provide only 3-4 availability zones per region, availability zone fault tolerance is limited to 1 zone outage (anything more requires more zones than are available in any typical region).
- Add or remove nodes in increments of 3 (1 node per zone); all zones have the same number of nodes.

#### Node

- YugabyteDB can continue to do reads and writes even in case of node outage, but this configuration is not resilient to cloud availability zone or region outages.
- Minimum of 3 nodes deployed in a single availability zone.
- Add or remove nodes in increments of 1.

### Sizing

The size of the cluster is based on the number of vCPUs. The default configuration for YugabyteDB Managed clusters includes 4 vCPUs per node. Each vCPU comes with 50GB of storage. A node has a minimum of 2 vCPUs with 4GB of memory per vCPU. For the cluster to be [fault tolerant](#fault-tolerance), you need a minimum of 3 nodes.

YugabyteDB Managed clusters support 15 simultaneous connections per vCPU. So a cluster with 3 nodes and 4 vCPUs per node can support 15 x 3 x 4 = 180 simultaneous connections.

| Cluster size (node x vCPU) | Maximum simultaneous connections |
| :--- | :--- |
| 3x2 | 90 |
| 3x4 | 180 |
| 3x8 | 360 |
| 6x2 | 180 |
| 6x4 | 360 |
| 6x8 | 720 |

For production clusters, a minimum of 3 nodes with 4 to 8 vCPUs per node is recommended.

During an update, one node is always offline. When sizing your cluster to your workload, ensure you have enough additional capacity to support rolling updates with minimal impact on application performance. You can also mitigate the effect of updates on performance by [scheduling them](../../cloud-clusters/cloud-maintenance/) during periods of lower traffic.

If your configuration doesn't match your performance requirements, you can [scale your cluster](../../cloud-clusters/configure-clusters/#scale-and-configure-clusters) after it is created. Depending on your performance requirements, you can increase the number of vCPUs per node (scale up, also referred to as vertical scaling), as well as the total number of nodes (scale out, also referred to as horizontal scaling). You can also increase the disk size per node. However, once increased, you can't lower the disk size per node.

YugabyteDB recommends vertical scaling until nodes have 16vCPUs, and horizontal scaling once nodes have 16 vCPUs. For example, for a 3-node cluster with 4 vCPUs per node, scale up to 8 vCPUs rather than adding a fourth node. For a 3-node cluster with 16 vCPUs per node, scale out by adding a 4th node.

Refer to [Scaling clusters](../../cloud-clusters/configure-clusters/).

### YugabyteDB version

By default, clusters are created using a stable release, taken from the [stable release series](../../../releases/versioning/#release-versioning-convention-for-stable-releases) of YugabyteDB. You can choose to deploy your dedicated cluster using the following tracks:

- Innovation - Updated more frequently, providing quicker access to new features.
- Production - Has less frequent updates, using select stable builds that have been tested longer in YugabyteDB Managed.

If you need a feature from the [preview release series](../../../releases/versioning/#release-versioning-convention-for-preview-releases) of YugabyteDB, contact {{% support-cloud %}} before you create your cluster. (Preview is also available for Sandbox clusters.)

Yugabyte manages upgrades for you. After you choose a track, database upgrades continue to take releases from the track you chose. For multi-node clusters, Yugabyte performs a rolling upgrade without any downtime. You can manage when Yugabyte performs maintenance and upgrades by configuring the [maintenance window](../../cloud-clusters/cloud-maintenance/) for your cluster.

{{< warning title="Important" >}}

There is currently no migration path from a preview release to a stable release.

{{< /warning >}}

### Staging cluster

Use a staging cluster for the following tasks:

- Verifying that your application is compatible with [database updates](#database-upgrades).
- Ensuring that your application correctly handles a rolling restart of the database without errors.
- Testing new features. Use your staging (also known as development, testing, pre-production, or canary) environment to try out new database features while your production systems are still running a previous version.
- Testing scaling operations and disaster recovery. Find out how your environment responds to a scaling operation, outages, or the loss of a node.

Create a staging cluster and configure your staging environment to connect to it. The staging cluster can be smaller than your production cluster, but you need to ensure that it has enough resources and capacity to handle a reasonable load. Sandbox clusters are too [resource-limited](../create-clusters/create-clusters-free/#limitations) for staging.

#### Database upgrades

Every YugabyteDB version in the stable track is tested for backwards compatibility. However, before upgrading your production cluster, it's good practice to first test your pre-production environment against database updates to ensure your applications are compatible. You want to make sure that an update doesn't have any performance impact on your application. For example, new functionality or a change in the optimizer could impact performance for a single query in a complex application.

When you are notified of an upcoming maintenance event, schedule the [maintenance windows](../../cloud-clusters/cloud-maintenance/) for the staging and production cluster so that you can validate updates against your applications in your pre-production environment _before_ upgrading your production cluster.

You can also set an [exclusion period](../../cloud-clusters/cloud-maintenance/#set-a-maintenance-exclusion-period) for your production cluster to postpone upgrades while you conduct testing.

If you identify a performance problem or regression with an update, set an exclusion period for your production cluster and contact {{% support-cloud %}}.

### Backups

YugabyteDB Managed provides a default recommended backup schedule (daily, with 8 day retention), and manages backups for you. You can [change the default schedule](../../cloud-clusters/backup-clusters/#schedule-backups), as well as perform [on-demand backups](../../cloud-clusters/backup-clusters/#on-demand-backups).

YugabyteDB Managed performs full cluster (all namespaces) level backups, and the backups are stored in the same region as your cluster. 100GB/month of basic backup storage is provided for every vCPU; more than that and overage charges apply. Refer to [Backup storage costs](../../cloud-admin/cloud-billing-costs/#backup-storage-costs).

### Security

Clusters are secure by default. You need to explicitly allow access to clusters by adding IP addresses of clients connecting to the cluster to the cluster IP allow list. Refer to [IP allow lists](../../cloud-secure-clusters/add-connections/).

If your applications are running in a VPC, deploy your cluster in a VPC to improve security and lower network latency. If you are using peering (AWS and GCP), you also need to add the CIDR ranges of peered application VPCs to your cluster IP allow list. If you are using a private link (AWS and Azure), you do not need to add addresses to the IP allow list.

Multi-region clusters, or clusters in Azure, must be deployed in VPCs; in AWS, each region or read replica must be deployed in its own VPC.

You need to create VPCs before you deploy the cluster. YugabyteDB Managed supports AWS, Azure, and GCP for VPCs. Refer to [VPC network](../cloud-vpcs/).

#### Database user authorization

YugabyteDB uses role-based access control to manage database access. When you create a cluster, YugabyteDB Managed adds a default database admin user (the credentials for this user are configurable).

After the cluster is provisioned, create a new database and [add users](../../cloud-secure-clusters/add-users/). You can create users specific to each connecting application, and restrict their access accordingly.

{{< note title="Note" >}}
In YSQL, the database admin user is not a full superuser. For security reasons, you do not have access to the Yugabyte or Postgres superusers, nor can you create users with superuser privileges.
{{< /note >}}

For more information on users and roles in YugabyteDB Managed, refer to [Database authorization in YugabyteDB Managed clusters](../../cloud-secure-clusters/cloud-users/).

## Pricing

The biggest factor in the price of a cluster is the number vCPUs.

Cluster charges are based on the total number of vCPUs used and how long they have been running. Cluster per-hour charges include [free allowances](../../cloud-admin/cloud-billing-costs/) for disk storage, backup storage, and data transfer. If you use more than the free allowance, you incur overages on top of the base vCPU capacity cost.

Before creating a cluster, you need to create your billing profile and add a payment method. Refer to [Manage your billing profile and payment method](../../cloud-admin/cloud-billing-profile/).

## Cloud provider regions

{{< tabpane text=true >}}

  {{% tab header="AWS" lang="aws" %}}

The following **AWS regions** are available:

- Cape Town (af-south-1)
- Hong Kong (ap-east-1)
- Tokyo (ap-northeast-1)
- Seoul (ap-northeast-2)
- Osaka (ap-northeast-3)
- Mumbai (ap-south-1)
- Hyderabad (ap-south-2)
- Singapore (ap-southeast-1)
- Sydney (ap-southeast-2)
- Jakarta (ap-southeast-3)
- Central (ca-central-1)
- Frankfurt (eu-central-1)
- Stockholm (eu-north-1)
- Milan (eu-south-1)
- Ireland (eu-west-1)
- London (eu-west-2)
- Paris (eu-west-3)
- Bahrain (me-south-1)
- Sao Paulo (sa-east-1)
- N. Virginia (us-east-1)
- Ohio (us-east-2)
- N. California (us-west-1)*
- Oregon (us-west-2)

\* Region has 2 availability zones only

  {{% /tab %}}

  {{% tab header="Azure" lang="azure" %}}

The following **Azure regions** are available:

- New South Wales (australiaeast)
- Sao Paulo State (brazilsouth)
- Toronto (canadacentral)
- Pune (centralindia)
- Iowa (centralus)
- Hong Kong (eastasia)
- Virginia (eastus)
- Virginia (eastus2)
- Paris (francecentral)
- Tokyo, Saitama (japaneast)
- Seoul (koreacentral)
- Ireland (northeurope)
- Norway (norwayeast)
- Johannesburg (southafricanorth)
- Texas (southcentralus)
- Singapore (southeastasia)
- Zurich (switzerlandnorth)
- Dubai (uaenorth)
- London (uksouth)
- Netherlands (westeurope)
- Washington (westus2)
- Phoenix (westus3)

  {{% /tab %}}

  {{% tab header="GCP" lang="gcp" %}}

The following **GCP regions** are available:

- Taiwan (asia-east1)
- Honk Kong (asia-east2)
- Tokyo (asia-northeast1)
- Osaka (asia-northeast2)
- Seoul (asia-northeast3)
- Mumbai (asia-south1)
- Delhi (asia-south2)
- Singapore (asia-southeast1)
- Jakarta (asia-southeast2)
- Sydney (australia-southeast1)
- Melbourne (australia-southeast2)
- Warsaw (europe-central2)
- Finland (europe-north1)
- Belgium (europe-west1)
- London (europe-west2)
- Frankfurt (europe-west3)
- Netherlands (europe-west4)
- Zurich (europe-west6)
- Montreal (northamerica-northeast1)
- Toronto (northamerica-northeast2)
- Sao Paulo (southamerica-east1)
- Iowa (us-central1)
- South Carolina (us-east1)
- N. Virginia (us-east4)
- Oregon (us-west1)
- Los Angeles (us-west2)
- Salt Lake City (us-west3)
- Las Vegas (us-west4)

  {{% /tab %}}

{{< /tabpane >}}

## Next steps

- [Create a cluster](../create-clusters/)
