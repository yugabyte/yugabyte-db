---
title: VPC network
headerTitle: VPC network
linkTitle: VPC network
description: Configure VPC networking in YugabyteDB Aeon.
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
headcontent: Set up VPC networking so that your clusters can communicate privately with applications
aliases:
  - /preview/yugabyte-cloud/cloud-network/vpc-peers/
  - /preview/yugabyte-cloud/cloud-secure-clusters/cloud-vpcs/
menu:
  preview_yugabyte-cloud:
    identifier: cloud-vpcs
    parent: cloud-basics
    weight: 20
type: indexpage
---

A Virtual Private Cloud (VPC) network allows applications running on instances on the same cloud provider as your YugabyteDB Aeon clusters to communicate with those clusters without traversing the public internet; all traffic stays in the cloud provider's network.

Use VPC networks to lower network latencies, make your application and database infrastructure more secure, and reduce network data transfer costs.

In YugabyteDB Aeon, a VPC network consists of the following components:

| Component | Description |
| :--- | :--- |
| [VPC](cloud-add-vpc/) | A VPC reserves a block of IP addresses on the cloud provider.<br />You deploy your cluster in a VPC. |
| [Peering connection](cloud-add-peering/) | Links the cluster VPC to an application VPC on the same cloud provider.<br />AWS and GCP only.<br />A peering connection is created for a VPC.<br />You need to add the IP address of your peered application VPC to the cluster [IP allow list](../../cloud-secure-clusters/add-connections/).<br/>Required for smart load balancing features of [YugabyteDB smart drivers](../../../drivers-orms/smart-drivers/#using-smart-drivers-with-yugabytedb-aeon). |
| [Private service endpoint](cloud-add-endpoint/) | Links the cluster endpoint to an application VPC endpoint, using the cloud provider's private linking service.<br />AWS and Azure only.<br />A private service endpoint (PSE) is added to a cluster; the cluster must be deployed in a VPC.<br/>No need to add the IP address of your application to the cluster IP allow list. |

Typically, you would either have a VPC network with peering, or use PSEs.

VPCs and peering connections are managed on the **VPC Network** tab of the **Networking** page.

{{< note title="Note" >}}

To connect a cluster to an application VPC using either a peering connection or a private service endpoint, you need to deploy the cluster in a dedicated VPC. You need to set up the dedicated VPC _before_ deploying your cluster.

VPC networking is not supported in Sandbox clusters.

{{< /note >}}

{{<index/block>}}

  {{<index/item
    title="Overview"
    body="How to choose the region and size for a VPC."
    href="cloud-vpc-intro/"
    icon="/images/section_icons/deploy/public-clouds.png">}}

  {{<index/item
    title="VPCs"
    body="Manage your account VPCs."
    href="cloud-add-vpc/"
    icon="/images/section_icons/index/deploy.png">}}

  {{<index/item
    title="Peering Connections"
    body="Manage your account peering connections."
    href="cloud-add-peering/"
    icon="/images/section_icons/quick_start/create_cluster.png">}}

  {{<index/item
    title="Peer VPCs"
    body="Connect your VPC to application VPCs on AWS and GCP using peering."
    href="cloud-add-vpc-aws/"
    icon="/images/section_icons/develop/api-icon.png">}}

  {{<index/item
    title="Private service endpoints"
    body="Manage private service endpoints (PSEs)."
    href="cloud-add-endpoint/"
    icon="/images/section_icons/quick_start/create_cluster.png">}}

  {{<index/item
    title="Set up a private link"
    body="Connect your VPC to application VPCs on AWS and Azure using a private link."
    href="managed-endpoint-aws/"
    icon="/images/section_icons/develop/api-icon.png">}}

{{</index/block>}}
