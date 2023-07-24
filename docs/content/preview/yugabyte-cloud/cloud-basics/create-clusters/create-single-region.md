---
title: Create a single-region cluster
linkTitle: Single region
description: Deploy dedicated single-region clusters in YugabyteDB Managed.
headcontent: Deploy availability zone- and node-level fault tolerant clusters
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

{{< youtube id="1eo7YXs3uTw" title="Deploy a fault tolerant cluster in YugabyteDB Managed" >}}

## Features

Single-region dedicated clusters include the following features:

- Multi node clusters with [replication factor](../../../../architecture/docdb-replication/replication/) (RF) of 3, and availability zone- and node-level fault tolerance.
- No limit on cluster size - choose any cluster size based on your use case.
- Horizontal and vertical scaling - add or remove nodes and vCPUs, and add storage to suit your production loads.
- VPC networking support.
- Automated and on-demand backups.
- Available in all [regions](../../create-clusters-overview/#cloud-provider-regions).
- Enterprise support.

## Prerequisites

- If you want to use dedicated VPCs for network isolation and security, create the VPC before you create your cluster. YugabyteDB Managed supports AWC and GCP for peering, and AWC and Azure for private linking. Clusters on Azure must be deployed in a VPC. Refer to [VPC network](../../cloud-vpcs/).
- Create a billing profile and add a payment method before you can create a Dedicated cluster. Refer to [Manage your billing profile and payment method](../../../cloud-admin/cloud-billing-profile/).

## Create a single-region cluster

To create a single-region cluster, on the **Clusters** page, click **Add Cluster**, and choose **Dedicated** to start the **Create Cluster** wizard.

The **Create Cluster** wizard has the following pages:

1. [General Settings](#general-settings)
1. [Cluster Setup](#cluster-setup)
1. [Network Access](#network-access)
1. [Security](#security)
1. [DB Credentials](#database-credentials)

{{% includeMarkdown "include-general-settings.md" %}}

### Cluster Setup

Select **Single-Region Deployment** and set the following options.

#### Select a fault tolerance for your cluster

![Add Cluster Wizard - Fault tolerance](/images/yb-cloud/cloud-addcluster-paid3.1.png)

Fault tolerance determines how resilient the cluster is to node and cloud zone failures. Choose one of the following:

- **Availability Zone Level**: Minimum of 3 nodes spread across multiple availability zones with a [replication factor](../../../../architecture/docdb-replication/replication/) (RF) of 3. YugabyteDB can continue to do reads and writes even in case of a cloud availability zone failure. This configuration provides the maximum protection for a data center failure. Recommended for production deployments. For horizontal scaling, nodes are scaled in increments of 3.
- **Node Level**: Minimum of 3 nodes deployed in a single availability zone with a RF of 3. YugabyteDB can continue to do reads and writes even in case of a node failure, but this configuration is not resilient to cloud availability zone outages. For horizontal scaling, you can scale nodes in increments of 1.
- **None**: Single node, with no replication or resiliency. Recommended for development and testing only.

You can't change the Fault tolerance of a cluster after it's created.

#### Choose a region and size your cluster

![Add Cluster Wizard - Region and size](/images/yb-cloud/cloud-addcluster-paid3.2.png)

**Region**: Choose the [region](../../create-clusters-overview/#cloud-provider-regions) where the cluster will be located.

**Nodes**: Enter the number of nodes for the cluster. Node and Availability Zone Level clusters have a minimum of 3 nodes; Availability Zone Level clusters increment by 3.

**vCPU/Node**: Enter the number of virtual CPUs per node.

**Disk size/Node**: Enter the disk size per node in GB.

Dedicated clusters support both horizontal and vertical scaling; you can change the cluster configuration after the cluster is created using the **Edit Configuration** settings. Refer to [Scale and configure clusters](../../../cloud-clusters/configure-clusters#infrastructure).

Monthly total costs for the cluster are based on the number of vCPUs and estimated automatically. **+ Usage** refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer. For information on how clusters are costed, refer to [Cluster costs](../../../cloud-admin/cloud-billing-costs/).

#### Configure VPC

![Add Cluster Wizard - Configure VPC](/images/yb-cloud/cloud-addcluster-paid3.3.png)

To use a VPC for network isolation and security, choose **Select a VPC to use a dedicated network isolated from others**, then select the VPC. Only VPCs using the selected cloud provider are listed. The VPC must be created before deploying the cluster. Refer to [VPC networking](../../cloud-vpcs/).

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
- [Build an application](../../../../develop/build-apps/)
- [Scale clusters](../../../cloud-clusters/configure-clusters/#single-region-clusters)
