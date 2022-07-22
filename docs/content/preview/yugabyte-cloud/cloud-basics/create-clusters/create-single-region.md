---
title: Create a single-region cluster
linkTitle: Single region
description: Deploy dedicated single-region clusters in YugabyteDB Managed.
headcontent:
aliases:
  - /preview/deploy/yugabyte-cloud/create-clusters/
  - /preview/yugabyte-cloud/create-clusters/
menu:
  preview_yugabyte-cloud:
    identifier: create-single-region
    parent: create-clusters
    weight: 50
type: docs
---

Single-region dedicated clusters support multi-node and highly available deployments and are suitable for production deployments.

{{< youtube id="qYMcNzWotkI" title="Deploy a fault tolerant cluster in YugabyteDB Managed" >}}

## Features

Single-region dedicated clusters include the following features:

- Multi node [replication factor](../../../../architecture/docdb-replication/replication/) (RF) of 3 clusters with availability zone and node level fault tolerance.
- No limit on cluster size - choose any cluster size based on your use case.
- Horizontal and vertical scaling - add or remove nodes and vCPUs, and add storage to suit your production loads.
- VPC networking support.
- Automated and on-demand backups.
- Available in all [regions](../../../release-notes#cloud-provider-regions).
- Enterprise support.

## Prerequisites

- If you want to use dedicated VPCs for network isolation and security, create the VPC before you create your cluster. YugabyteDB Managed supports AWC and GCP for peering. Refer to [VPC networking](../../cloud-vpcs/).
- Create a billing profile and add a payment method before you can create a Dedicated cluster. Refer to [Manage your billing profile and payment method](../../../cloud-admin/cloud-billing-profile/).

## Create a single-region cluster

To create a single-region cluster, on the **Clusters** page, click **Add Cluster**, and choose **Dedicated** to start the **Create Cluster** wizard.

The **Create Cluster** wizard has the following pages:

1. [General Settings](#general-settings)
1. [Cluster Setup](#cluster-setup)
1. [DB Credentials](#database-credentials)

### General Settings

![Add Cluster Wizard - General Settings](/images/yb-cloud/cloud-addcluster-free2.png)

Set the following options:

- **Cluster Name**: Enter a name for the cluster.
- **Provider**: Choose a cloud provider - AWS or GCP.
- **[Database Version](../../../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on)**: By default, clusters are deployed using a stable release. If you want to use a preview release for a Dedicated cluster, click **Optional Settings** and choose a release. Before deploying a production cluster using a preview release, contact {{% support-cloud %}}. If you have arranged a custom build with Yugabyte, it will also be listed here.

### Cluster Setup

Select **Single-Region Deployment** and set the following options.

#### Select a fault tolerance for your cluster

![Add Cluster Wizard - Fault tolerance](/images/yb-cloud/cloud-addcluster-paid3.1.png)

Fault tolerance determines how resilient the cluster is to node and cloud zone failures. Choose one of the following:

- **None** - single node, with no replication or resiliency. Recommended for development and testing only.
- **Node Level** - a minimum of 3 nodes deployed in a single availability zone with a [replication factor](../../../../architecture/docdb-replication/replication/) (RF) of 3. YugabyteDB can continue to do reads and writes even in case of a node failure, but this configuration is not resilient to cloud availability zone outages. For horizontal scaling, you can scale nodes in increments of 1.
- **Availability Zone Level** - a minimum of 3 nodes spread across multiple availability zones with a RF of 3. YugabyteDB can continue to do reads and writes even in case of a cloud availability zone failure. This configuration provides the maximum protection for a data center failure. Recommended for production deployments. For horizontal scaling, nodes are scaled in increments of 3.

#### Choose a region and size your cluster

![Add Cluster Wizard - Region and size](/images/yb-cloud/cloud-addcluster-paid3.2.png)

**Region**: Choose the [region](../../../release-notes#cloud-provider-regions) where the cluster will be located, or click **Request a multi-region cluster** to contact Yugabyte Support to arrange multi-region deployment.

**Nodes** - enter the number of nodes for the cluster. Node and Availability zone level clusters have a minimum of 3 nodes; Availability zone level clusters increment by 3.

**vCPU/Node** - enter the number of virtual CPUs per node.

**Disk size/Node** - enter the disk size per node in GB.

#### Configure VPC

  ![Add Cluster Wizard - Configure VPC](/images/yb-cloud/cloud-addcluster-paid3.3.png)

  To use a VPC for network isolation and security, select **Deploy this cluster in a dedicated VPC**, then select the VPC. Only VPCs using the selected cloud provider are listed. The VPC must be created before deploying the cluster. Refer to [VPC networking](../../cloud-vpcs/).

Monthly total costs for the cluster are estimated automatically. **+ Usage** refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer. For information on how clusters are costed, refer to [Cluster costs](../../../cloud-admin/cloud-billing-costs/).

Dedicated clusters support both horizontal and vertical scaling; you can change the cluster configuration after the cluster is created using the **Edit Configuration** settings. Refer to [Configure clusters](../../../cloud-clusters/configure-clusters#infrastructure).

### Database Credentials

The database admin credentials are required to connect to the YugabyteDB database that is installed on the cluster.

You can use the default credentials generated by YugabyteDB Managed, or add your own.

For security reasons, the admin user does not have YSQL superuser privileges, but does have sufficient privileges for most tasks. For more information on database roles and privileges in YugabyteDB Managed, refer to [Database authorization in YugabyteDB Managed clusters](../../../cloud-secure-clusters/cloud-users/).

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

- [Assign IP allow lists](../../../cloud-secure-clusters/add-connections/)
- [Connect to your cluster](../../../cloud-connect/)
- [Add database users](../../../cloud-secure-clusters/add-users/)
- [Build an application](../../../cloud-quickstart/cloud-build-apps/)
- [Database authorization in YugabyteDB Managed clusters](../../../cloud-secure-clusters/cloud-users/)
