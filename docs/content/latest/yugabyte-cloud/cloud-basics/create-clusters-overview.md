---
title: Planning a cluster
linkTitle: Planning a cluster
description: Planning a cluster in Yugabyte Cloud.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: create-clusters-overview
    parent: cloud-basics
    weight: 10
isTocNested: true
showAsideToc: true
---

Before deploying a production cluster, you need to consider the following factors.

## Best practices

The following best practices are recommended for production clusters.

Location and provider
: Deploy your cluster in a virtual private cloud (VPC). You need to create the VPC before you deploy the cluster. Yugabyte Cloud supports AWS and GCP for VPCs. Refer to [VPC network](../cloud-vpcs/).
: Locate your VPC and cluster in the same region as your application VPC.

Fault tolerance
: Availability zone (AZ) level - minimum of three nodes across multiple AZs, with a replication factor of 3.

Sizing
: For most production applications, at least 3 nodes with 4 to 8 vCPUs per node.
: When scaling your cluster, for best results increase node size up to 16 vCPUs before adding more nodes. For example, if you have a 3-node cluster with 4 vCPUs per node, consider scaling up to 8 or 16 vCPUs before adding a fourth node.

YugabyteDB version
: Use the **Stable** release track.

Security and authorization
: Yugabyte Cloud clusters are secure by default. After deploying, set up IP allow lists and add database users to allow clients, applications, and application VPCs to connect. Refer to [IP allow lists](../../cloud-secure-clusters/add-connections/).

## In depth

### Provider and region

#### Provider

Yugabyte Cloud supports AWS and GCP. Your choice of provider will depend primarily on where your existing applications are hosted. Yugabyte Cloud pricing is the same for both.

#### Region

For best performance as well as lower data transfer costs, you want to minimize transfers between providers, and between provider regions. Do this by locating your cluster as close to your applications as possible:

- Use the same cloud provider as your application.
- Locate your cluster in the same region as your application.

For lowest possible network latency and data transfer costs, deploy your cluster in a VPC on the same cloud provider as your application VPC and peer it with the application VPC. This configuration also provides the best security.

For a list of supported regions, refer to [Cloud provider regions](../../release-notes/#cloud-provider-regions).

### Fault tolerance

The _fault tolerance_ determines how resilient the cluster is to node and cloud zone failures. Yugabyte Cloud provides the following options for providing replication and redundancy:

- **Availability zone level**. Includes a minimum of 3 nodes spread across multiple availability zones with a [replication factor](../../../architecture/docdb-replication/replication/) (RF) of 3. YugabyteDB can continue to do reads and writes even in case of a cloud availability zone failure. This configuration provides the maximum protection for a data center failure.

- **Node level**. Includes a minimum of 3 nodes deployed in a single availability zone with a RF of 3. YugabyteDB can continue to do reads and writes even in case of a node failure, but this configuration is not resilient to cloud availability zone outages.

- **Region level**. Yugabyte supports multi-region clusters with regional fault tolerance. Contact {{<support-cloud>}} for help configuring regional fault tolerance.

Although you can't change the cluster fault tolerance after the cluster is created, you can scale horizontally as follows:

- For Availability zone level, you can add or remove nodes in increments of 3.
- For Node level, you can add or remove nodes in increments of 1.

For production clusters, Availability zone level is recommended.

For application development and testing, you can set fault tolerance to **None** to create a single-node cluster. Single-node clusters can't be scaled.

### Sizing

The size of the cluster is based on the number of vCPUs. The basic configuration for Yugabyte Cloud clusters includes 2 vCPUs per node. Each vCPU comes with 50GB of storage. A node has a minimum of 2 vCPUs, and 2GB of RAM per vCPU. For the cluster to be [fault tolerant](#fault-tolerance), you need a minimum of 3 nodes.

Depending on your performance requirements, you can increase the number of vCPUs per node, as well as the total number of nodes. You can also increase the disk size per node. However, once increased, you can't lower the disk size per node.

Yugabyte Cloud supports both vertical and horizontal scaling. If your configuration doesn't match your performance requirements, you can change these values after the cluster is created (increasing or decreasing vCPUs and increasing storage, and adding or removing nodes). Refer to [Scaling clusters](../../cloud-clusters/configure-clusters/).

For production clusters, a minimum of 3 nodes with 4 to 8 vCPUs per node is recommended.

### YugabyteDB version

By default, clusters are created using a stable release, taken from the [stable release series](../../../releases/versioning/#stable-releases) of YugabyteDB.

You can choose to deploy your cluster using an edge release for development and testing. Edge releases are typically taken from the [latest release series](../../../releases/versioning/#latest-releases) of YugabyteDB, though they can also include a recently released stable release.

If you need a feature from an edge release (that isn't yet available in a stable release) for a production deployment, contact {{<support-cloud>}} before you create your cluster.

Yugabyte manages upgrades for you. After you choose a track, database upgrades continue to take releases from the track you chose. For multi-node clusters, Yugabyte performs a rolling upgrade without any downtime. You can manage when Yugabyte performs maintenance and upgrades by configuring the [maintenance window](../../cloud-clusters/cloud-maintenance/) for your cluster.

### Backups

Yugabyte Cloud provides a default recommended backup schedule (daily, with 8 day retention), and manages backups for you. You can [change the default schedule](../../cloud-clusters/backup-clusters/#schedule-backups), as well as perform [on-demand backups](../../cloud-clusters/backup-clusters/#on-demand-backups).

Yugabyte Cloud performs full cluster (all namespaces) level backups, and the backups are stored in the same region as your cluster. 100GB/month of basic backup storage is provided for every vCPU; more than that and overage charges apply. Refer to [Backup storage costs](../../cloud-admin/cloud-billing-costs/#backup-storage-costs).

### Security

Clusters are secure by default. You need to explicitly allow access to clusters by adding IP addresses of clients connecting to the cluster to the cluster IP allow list. Refer to [IP allow lists](../../cloud-secure-clusters/add-connections/).

If your applications are running in a VPC, deploy your cluster in a VPC to improve security and lower network latency. You also need to add the CIDR ranges of any application VPCs to your cluster IP allow list. You need to create the VPC before you deploy the cluster. Yugabyte Cloud supports AWS and GCP for VPCs. Refer to [VPC network](../../cloud-basics/cloud-vpcs/).

#### User authorization

YugabyteDB uses role-based access control to manage database access. When you create a cluster, Yugabyte Cloud adds a default admin user (the credentials for this user are configurable).

After the cluster is provisioned, create a new database and [add users](../../cloud-secure-clusters/add-users/). You can create users specific to each connecting application, and restrict their access accordingly.

{{< note title="Note" >}}
In YSQL, the admin user is not a full superuser. For security reasons, you do not have access to the Yugabyte or Postgres superusers, nor can you create users with superuser privileges.
{{< /note >}}

For more information on users and roles in Yugabyte Cloud, refer to [Database authorization in Yugabyte Cloud clusters](../../cloud-secure-clusters/cloud-users/).

## Pricing

The biggest factor in the price of a cluster is the number vCPUs.

Cluster charges are based on the total number of vCPUs used and how long they have been running. Cluster per-hour charges include [free allowances](../../cloud-admin/cloud-billing-costs/) for disk storage, backup storage, and data transfer. If you use more than the free allowance, you incur overages on top of the base vCPU capacity cost.

Before creating a cluster, you need to create your billing profile and add a payment method. Refer to [Manage your billing profile and payment method](../../cloud-admin/cloud-billing-profile/).

If you're interested in evaluating Yugabyte Cloud for production use and would like trial credits to conduct a proof-of-concept (POC), contact {{<support-cloud>}}.

## Next steps

- [Create a cluster](../create-clusters/)
