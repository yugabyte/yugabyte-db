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

## Location and provider

Yugabyte Cloud supports AWS and GCP.

For lowest data transfer costs, use the same cloud provider and locate your cluster in the same region as the applications that will be connecting to the cluster. For a list of supported regions, refer to [Cloud provider regions](../../release-notes/#cloud-provider-regions).

## Sizing

The basic configuration for Yugabyte Cloud clusters includes 2 vCPUs per node. Each vCPU comes with 50GB of storage. A node has a minimum of 2 vCPUs.

Depending on your performance requirements, you can increase the number of vCPUs per node, as well as the total number of nodes. For production clusters, a minimum of 3 nodes with 2 vCPUs per node is recommended.

Yugabyte Cloud supports both vertical and horizontal scaling, so you can change these values after the cluster is created if your configuration doesn't match your performance requirements. This includes increasing or decreasing vCPUs and increasing storage, and adding or removing nodes. Refer to [Scaling clusters](../../cloud-clusters/configure-clusters/).

### Fault tolerance

The **Fault Tolerance** determines how resilient the cluster is to node and cloud zone failures. Yugabyte Cloud provides the following options for providing replication and redundancy:

- **Node Level**. Includes a minimum of 3 nodes deployed in a single availability zone with a [replication factor](../../../architecture/docdb-replication/replication/) (RF) of 3. YugabyteDB can continue to do reads and writes even in case of a node failure, but this configuration is not resilient to cloud availability zone outages.

- **Availability Zone Level**. Includes a minimum of 3 nodes spread across multiple availability zones with a RF of 3. YugabyteDB can continue to do reads and writes even in case of a cloud availability zone failure. This configuration provides the maximum protection for a data center failure.

- **Region Level**. Yugabyte supports multi-region clusters with regional fault tolerance. Region level can’t be self-provisioned in Yugabyte Cloud. Contact Yugabyte Support for assistance.

Although you can't change the cluster fault tolerance once the cluster is created, you can scale horizontally as follows:

- For Node Level, you can scale nodes in increments of 1.
- For Availability Zone Level, you can scale nodes in increments of 3.

For production clusters, Availability Zone Level is recommended.

For application development and testing, you can set fault tolerance to **None** to create a single-node cluster. Single-node clusters can't be scaled.

## Security

If your applications are running in a virtual private cloud (VPC), deploy your cluster in a VPC to improve security and lower network latency. You need to create the VPC before you deploy the cluster. Yugabyte Cloud supports AWS and GCP for VPCs. Refer to [VPC network](../../cloud-secure-clusters/cloud-vpcs/).

By default, access to clusters is restricted to IP addresses that you specify in IP allow lists. Once the cluster is deployed, add the IP addresses of the clients to the cluster allow list. This includes the _CIDR ranges of any application VPCs_, as well as addresses of users connecting to the cluster using a client. Refer to [IP allow lists](../../cloud-secure-clusters/add-connections/).

## YugabyteDB version

By default, clusters are created using a recent release from the [stable release series](../../../releases/versioning/#stable-releases) of YugabyteDB.

You can opt to create your database using an edge release for development and testing. Edge releases are typically taken from the [latest release series](../../../releases/versioning/#latest-releases) of YugabyteDB, though they can also include a recently released stable release.

If you require a feature from an edge release (that is not available in a stable release) for a production deployment, first contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

Once you choose a release track (edge or stable), database upgrades will continue to take releases from the track you chose.

## Pricing

The biggest factor in the price of a cluster is its size. Cluster charges are based on the total number of vCPUs used and how long they have been running.

Cluster per-hour charges include free allowances for disk storage, backup storage, and data transfer. If you use more than the free allowance, you incur overages on top of the base vCPU capacity cost. For more details, refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

Before creating a cluster, you need to create your billing profile and add a payment method. Refer to [Manage your billing profile and payment method](../../cloud-admin/cloud-billing-profile/).

If you are interested in evaluating Yugabyte Cloud for production use and would like trial credits to conduct a proof-of-concept (POC), contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

## Next steps

- [Create a cluster](../create-clusters/)
