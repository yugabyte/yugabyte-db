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

For the lowest data transfer costs, you should minimize transfers between providers, and between provider regions. Use the same cloud provider and locate your cluster in the same region as the applications that will be connecting to the cluster. For a list of supported regions, refer to [Cloud provider regions](../../release-notes/#cloud-provider-regions).

## Sizing

The basic configuration for Yugabyte Cloud clusters includes 2 vCPUs per node. Each vCPU comes with 50GB of storage. A node has a minimum of 2 vCPUs.

Depending on your performance requirements, you can increase the number of vCPUs per node, as well as the total number of nodes. For production clusters, a minimum of 3 nodes with 2 vCPUs per node is recommended.

Yugabyte Cloud supports both vertical and horizontal scaling, so you can change these values after the cluster is created if your configuration doesn't match your performance requirements. This includes increasing or decreasing vCPUs and increasing storage, and adding or removing nodes. Refer to [Scaling clusters](../../cloud-clusters/configure-clusters/).

### Fault tolerance

A cluster's **fault tolerance** determines how resilient it is to node and cloud zone failures. Yugabyte Cloud provides the following options for providing replication and redundancy:

- **Node level**. Includes a minimum of 3 nodes deployed in a single availability zone with a [replication factor](../../../architecture/docdb-replication/replication/) (RF) of 3. YugabyteDB can continue to do reads and writes even in case of a node failure, but this configuration is not resilient to cloud availability zone outages.

- **Availability zone level**. Includes a minimum of 3 nodes spread across multiple availability zones with a RF of 3. YugabyteDB can continue to do reads and writes even in case of a cloud availability zone failure. This configuration provides the maximum protection for a data center failure.

- **Region level**. Yugabyte supports multi-region clusters with regional fault tolerance. Contact Yugabyte Support for assistance configuring regional fault tolerance.

Although you can't change the cluster fault tolerance after the cluster is created, you can scale horizontally as follows:

- For Node level, you can add or remove nodes in increments of 1.
- For Availability zone level, you can add or remove nodes in increments of 3.

For production clusters, Availability zone level is recommended.

For application development and testing, you can set fault tolerance to **None** to create a single-node cluster. Single-node clusters can't be scaled.

## Security

If your applications run in a virtual private cloud (VPC), deploy your cluster in a VPC to improve security. A VPC network also has the advantage of lowering network latency. You need to create the VPC before you deploy the cluster. Yugabyte Cloud supports AWS and GCP for VPCs. Refer to [VPC network](../../cloud-secure-clusters/cloud-vpcs/).

By default, access to clusters is restricted to IP addresses that you specify in IP allow lists. After the cluster is deployed, add the IP addresses of the clients to the cluster allow list. This includes the _CIDR ranges of any application VPCs_, as well as addresses of users connecting to the cluster using a client. Refer to [IP allow lists](../../cloud-secure-clusters/add-connections/).

## YugabyteDB version

By default, clusters are created using a stable release, taken from the [stable release series](../../../releases/versioning/#stable-releases) of YugabyteDB.

You can choose to deploy your cluster using an edge release for development and testing. Edge releases are typically taken from the [latest release series](../../../releases/versioning/#latest-releases) of YugabyteDB, though they can also include a recently released stable release.

If you need a feature from an edge release (that isn't yet available in a stable release) for a production deployment, contact {{<support-cloud>}} before you create your cluster.

Yugabyte automatically upgrades the database with releases from the track (stable or edge) that you selected when deploying the cluster.

For more information on YugabyteDB versions used in Yugabyte Cloud, refer to [What version of YugabyteDB does my cluster run on?](../../cloud-faq/#what-version-of-yugabytedb-does-my-cluster-run-on)

## Pricing

The biggest factor in the price of a cluster is its size. Cluster charges are based on the total number of vCPUs used and how long they have been running.

Cluster per-hour charges include free allowances for disk storage, backup storage, and data transfer. If you use more than the free allowance, you incur overages on top of the base vCPU capacity cost. For more details, refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

Before creating a cluster, you need to create your billing profile and add a payment method. Refer to [Manage your billing profile and payment method](../../cloud-admin/cloud-billing-profile/).

If you're interested in evaluating Yugabyte Cloud for production use and would like trial credits to conduct a proof-of-concept (POC), contact {{<support-cloud>}}.

## Next steps

- [Create a cluster](../create-clusters/)
